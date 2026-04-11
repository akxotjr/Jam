#include "pch.h"
#include "jamnet/sync/replication/ServerPhysicsSystem.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ServerInputSystem.h"
#include "jamnet/sync/networld/ServerNetWorld.h"

namespace jam::net
{
	ServerPhysicsSystem::ServerPhysicsSystem(entt::registry& world, px::IPhysicsFacade* physics)
		: m_world(world), m_physics(physics)
	{
	}

	void ServerPhysicsSystem::Init()
	{
		m_tickDebt = 0;
		m_tickFiberRunning = false;
	}

	void ServerPhysicsSystem::Tick()
	{
		if (!m_physics) return;

		auto runOneTick = [this]()
			{
				ApplyInputs();
				Simulate();
				SyncActiveTransforms();
			};

		if (m_tickDebt < m_tickDebtCap)
			++m_tickDebt;

		auto& shard = SHARD_LOCAL_CHECKED();
		auto* sched = shard.scheduler;

		if (!sched)
		{
			while (m_tickDebt > 0)
			{
				--m_tickDebt;
				runOneTick();
			}

			SyncTransforms();
			return;
		}

		if (m_tickFiberRunning)
			return;

		m_tickFiberRunning = true;

		sched->SpawnFiber(
			[this]()
			{
				try
				{
					uint32 burst = 0;
					while (m_tickDebt > 0 && burst < m_tickBurstBudget)
					{
						--m_tickDebt;
						ApplyInputs();
						Simulate();
						SyncActiveTransforms();
						++burst;
					}
				}
				catch (...)
				{
					m_tickFiberRunning = false;
					throw;
				}

				m_tickFiberRunning = false;
			},
			FiberDesc{ .name = "ServerPhysics.TickFiber" }
		);
	}

	void ServerPhysicsSystem::SpawnActor(entt::entity e, const px::SpawnDesc& desc) const
	{
		if (!m_physics || !m_world.valid(e))
			return;

		const px::ObjectId oid = MakeObjectId(e);
		if (!m_physics->Spawn(oid, desc)) 
			return;

		const px::eBodyType bodyType = desc.IsCharacter() ? px::eBodyType::Character : px::eBodyType::Rigid;
		
		if (m_physics->IsStepPending())
		{
			m_pendingActorOps.push_back(PendingActorOp{
				.type	  = PendingActorOp::eType::Spawn,
				.e        = e,
				.bodyType = bodyType
			});
			return;
		}
		
		m_world.emplace<NetActorBodyType>(e, NetActorBodyType{ bodyType });
		m_world.emplace<PhysicsSpawnedTag>(e);

		if (bodyType == px::eBodyType::Character)
		{
			auto& cs = m_world.emplace<px::CharacterState>(e);
			JAM_ASSERT(m_physics->GetCharacterState(oid, cs))

			const uint64 controller = m_world.get<ControlTag>(e).userId;
			const uint64 owner		= m_world.get<OwnershipTag>(e).userId;

			if (owner && owner == controller)
			{
				m_world.emplace<px::CharacterInput>(e);
			}
		}
		else
		{
			auto& rs = m_world.emplace<px::RigidState>(e);
			JAM_ASSERT(m_physics->GetRigidState(oid, rs))
		}
	}

	void ServerPhysicsSystem::DespawnActor(entt::entity e) const
	{
		if (!m_physics || !m_world.valid(e))
			return;

		const px::ObjectId id = MakeObjectId(e);
		if (!m_physics->Despawn(id))
			return;

		if (m_physics->IsStepPending())
		{
			const px::eBodyType bodyType =
				m_world.all_of<NetActorBodyType>(e) ? m_world.get<NetActorBodyType>(e).body : px::eBodyType::None;

			m_pendingActorOps.push_back(PendingActorOp{
				.type	  = PendingActorOp::eType::Despawn,
				.e		  = e,
				.bodyType = bodyType
			});
			return;
		}

		m_world.erase<PhysicsSpawnedTag>(e);
	}

	void ServerPhysicsSystem::ApplyInputs() const
	{
		auto view = m_world.view<ControlTag, px::CharacterInput>();
		auto* inputSys = m_world.ctx().find<ServerInputSystem>();

		for (auto e : view)
		{
			const auto& control = view.get<ControlTag>(e);
			auto& input = view.get<px::CharacterInput>(e);
			m_physics->ApplyCharacterInput(MakeObjectId(e), input);

			if (inputSys && control.userId != 0)
				inputSys->MarkInputApplied(control.userId);
		}
	}

	void ServerPhysicsSystem::Simulate()
	{
		if (!m_physics) return;

		auto& shard = SHARD_LOCAL_CHECKED();
		auto* sched = shard.scheduler;

		const bool inFiber = sched && (sched->Current() != 0);

		const uint64 awaitKey = inFiber ? ++m_awaitSeq : 0;

		// BeginStep이 true를 반환하면 PhysX Task가 Shard에 제출되었으므로 파이버를 Suspend 합니다.
		if (m_physics->BeginSimulate(SIMULATION_TICK_SEC, awaitKey) && inFiber)
			sched->Suspend(awaitKey, NOW_NS() + 2_s);

		// 파이버가 Resume 된 후 (또는 동기 실행 시) 결과를 가져옵니다.
		m_physics->EndSimulate();
		CommitPendingActorOps();
		HandleProjectileLifecycleEvents();
	}

	void ServerPhysicsSystem::SyncActiveTransforms() const
	{
		if (!m_physics) return;

		m_world.clear<ReplicationActiveTag>();

		// PopActiveList = onAdvance(dynamic) + dirty set(kinematic / character move / setGlobalPose)
		const auto activeList = m_physics->PopActiveList();
		if (activeList.empty()) return;

		for (const px::ObjectId id : activeList)
		{
			const entt::entity e = static_cast<entt::entity>(id);
			if (!m_world.valid(e)) continue;

			m_world.emplace<ReplicationActiveTag>(e);

			const auto bodyType = m_world.get<NetActorBodyType>(e).body;

			if (bodyType == px::eBodyType::Character)
			{
				px::CharacterState cs{};
				if (m_physics->GetCharacterState(id, cs))
				{
					if (auto* state = m_world.try_get<px::CharacterState>(e))
						*state = cs;
				}
			}
			else
			{
				px::RigidState rs{};
				if (m_physics->GetRigidState(id, rs))
				{
					if (auto* state = m_world.try_get<px::RigidState>(e))
						*state = rs;
				}
			}
		}
	}

	void ServerPhysicsSystem::SyncTransforms() const
	{
		if (!m_physics)
			return;

		auto view = m_world.view<NetId, NetActorBodyType, PhysicsSpawnedTag>();

		for (auto e : view)
		{
			const px::ObjectId oid = MakeObjectId(e);
			const auto bodyType = view.get<NetActorBodyType>(e).body;

			if (bodyType == px::eBodyType::Character)
			{
				px::CharacterState cs{};
				if (m_physics->GetCharacterState(oid, cs))
				{
					auto& state = m_world.get<px::CharacterState>(e);
					state = cs;
				}
			}
			else
			{
				px::RigidState rs{};
				if (m_physics->GetRigidState(oid, rs))
				{
					auto& state = m_world.get<px::RigidState>(e);
					state = rs;
				}
			}
		}
	}

	void ServerPhysicsSystem::CommitPendingActorOps()
	{
		if (m_pendingActorOps.empty())
			return;

		auto ops = std::move(m_pendingActorOps);
		m_pendingActorOps.clear();

		for (const auto& op : ops)
		{
			if (!m_world.valid(op.e))
				continue;

			if (op.type == PendingActorOp::eType::Spawn)
			{
				const px::ObjectId oid = MakeObjectId(op.e);

				m_world.emplace_or_replace<NetActorBodyType>(op.e, NetActorBodyType{ op.bodyType });
				m_world.emplace_or_replace<PhysicsSpawnedTag>(op.e);

				if (op.bodyType == px::eBodyType::Character)
				{
					auto& cs = m_world.emplace_or_replace<px::CharacterState>(op.e);
					JAM_ASSERT(m_physics->GetCharacterState(oid, cs))

					const uint64 controller = m_world.get<ControlTag>(op.e).userId;
					const uint64 owner	    = m_world.get<OwnershipTag>(op.e).userId;

					if (owner && owner == controller)
					{
						m_world.emplace_or_replace<px::CharacterInput>(op.e);
					}
				}
				else if (op.bodyType == px::eBodyType::Rigid)
				{
					auto& rs = m_world.emplace_or_replace<px::RigidState>(op.e);
					JAM_ASSERT(m_physics->GetRigidState(oid, rs))
				}
			}
			else
			{
				m_world.erase<PhysicsSpawnedTag>(op.e);
			}
		}
	}

	void ServerPhysicsSystem::HandleProjectileLifecycleEvents() const
	{
		if (!m_physics)
			return;

		auto* nwPtr = m_world.ctx().find<ServerNetWorld*>();
		if (!nwPtr || !*nwPtr)
			return;

		ServerNetWorld* netWorld = *nwPtr;
		std::unordered_set<uint32> pending;

		for (const px::PhysicsEvent& evt : m_physics->ConsumePhysicsEvents())
		{
			if (evt.type != px::ePhysicsEventType::ProjectileHit
				&& evt.type != px::ePhysicsEventType::ProjectileLifetimeExpired)
			{
				continue;
			}

			const entt::entity e = static_cast<entt::entity>(evt.sourceId);
			if (e == entt::null || !m_world.valid(e))
				continue;

			const auto* netId = m_world.try_get<NetId>(e);
			if (!netId || !netId->IsValid())
				continue;

			if (!pending.insert(netId->Raw()).second)
				continue;

			netWorld->DespawnActorImmediate(*netId, 0);
		}
	}
}
