#include "pch.h"
#include "jamnet/sync/replication/ServerPhysicsSystem.h"

#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ServerAoiSystem.h"
#include "jamnet/sync/replication/ServerInputSystem.h"
#include "jamnet/sync/networld/ServerPhysicalWorld.h"
#include "jamnet/sync/replication/WorldContext.h"

namespace jam::net
{
	namespace
	{
		bool TryReadValidatedCharacterState(const px::IPhysicsFacade* physics, px::ObjectId objectId, OUT px::CharacterState& outState)
		{
			if (!physics->GetCharacterState(objectId, outState))
				return false;

			const bool finite = outState.IsFinite();
			JAM_ASSERT(finite && "Physics readback produced non-finite CharacterState");
			return finite;
		}

		bool TryReadValidatedRigidState(const px::IPhysicsFacade* physics, px::ObjectId objectId, OUT px::RigidState& outState)
		{
			if (!physics->GetRigidState(objectId, outState))
				return false;

			outState.pose.q.Normalize();

			const bool finite = outState.IsFinite();
			JAM_ASSERT(finite && "Physics readback produced non-finite RigidState");
			return finite;
		}
	}

	ServerPhysicsSystem::ServerPhysicsSystem(entt::registry& world, px::IPhysicsFacade* physics)
		: m_world(world), m_physics(physics)
	{
	}

	void ServerPhysicsSystem::Init()
	{
		m_tickDebt		   = 0;
		m_tickFiberRunning = false;
		m_lastActiveEntities.clear();
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

		auto& shard = CurrentShardLocalChecked();
		auto* sched = shard.scheduler;

		if (!sched)
		{
			while (m_tickDebt > 0)
			{
				--m_tickDebt;
				runOneTick();
			}

			SyncTransforms(); // ?
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

		if (m_physics->IsStepPending())
		{
			m_pendingActorOps.push_back(PendingActorOp{
				.type	  = PendingActorOp::eType::Spawn,
				.e        = e,
			});
			return;
		}

		m_world.emplace<PhysicsSpawnedTag>(e);

		if (auto* cs = m_world.try_get<CharAuthorityState>(e))
		{
			if (!TryReadValidatedCharacterState(m_physics, oid, cs->state))
			{
				m_world.remove<PhysicsSpawnedTag>(e);

				// todo
				return;
			}
		}
		else if (auto* rs = m_world.try_get<RigidAuthorityState>(e))
		{
			if (!TryReadValidatedRigidState(m_physics, oid, rs->state))
			{
				m_world.remove<PhysicsSpawnedTag>(e);
				
				//todo
				return;
			}
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
			m_pendingActorOps.push_back(PendingActorOp{
				.type	  = PendingActorOp::eType::Despawn,
				.e		  = e,
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

		auto& shard = CurrentShardLocalChecked();
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
		m_lastActiveEntities.clear();

		// PopActiveList = onAdvance(dynamic) + dirty set(kinematic / character move / setGlobalPose)
		const auto activeList = m_physics->PopActiveList();
		if (activeList.empty()) return;

		px::CharacterState csBuf{};
		px::RigidState	   rsBuf{};

		for (const px::ObjectId id : activeList)
		{
			const entt::entity e = static_cast<entt::entity>(id);
			if (e == entt::null || !m_world.valid(e)) continue;

			if (auto* cs = m_world.try_get<CharAuthorityState>(e))
			{
				if (TryReadValidatedCharacterState(m_physics, id, csBuf))
				{
					cs->state = csBuf;
					m_world.emplace<ReplicationActiveTag>(e);
					m_lastActiveEntities.push_back(e);
				}
			}
			else if (auto* rs = m_world.try_get<RigidAuthorityState>(e))
			{
				if (TryReadValidatedRigidState(m_physics, id, rsBuf))
				{
					rs->state = rsBuf;
					m_world.emplace<ReplicationActiveTag>(e);
					m_lastActiveEntities.push_back(e);
				}
			}
		}
	}

	void ServerPhysicsSystem::SyncTransforms() const
	{
		if (!m_physics) return;

		auto view = m_world.view<NetId, NetActorBodyType, PhysicsSpawnedTag>();

		for (auto e : view)
		{
			const px::ObjectId oid = MakeObjectId(e);
			const auto bodyType = view.get<NetActorBodyType>(e).body;

			if (bodyType == px::eBodyType::Character)
			{
				px::CharacterState cs{};
				if (TryReadValidatedCharacterState(m_physics, oid, cs))
				{
					if (auto* state = m_world.try_get<CharAuthorityState>(e))
						state->state = cs;
				}
			}
			else
			{
				px::RigidState rs{};
				if (TryReadValidatedRigidState(m_physics, oid, rs))
				{
					if (auto* state = m_world.try_get<RigidAuthorityState>(e))
						state->state = rs;
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


		px::CharacterState	csBuf{};
		px::RigidState		rsBuf{};
		auto* aoi = m_world.ctx().find<ServerAoiSystem>();

		for (const auto& op : ops)
		{
			if (!m_world.valid(op.e))
				continue;

			const px::ObjectId oid = MakeObjectId(op.e);
			m_world.emplace_or_replace<PhysicsSpawnedTag>(op.e);

			switch (op.type)
			{
			case PendingActorOp::eType::Spawn:
			{
				bool spawned = false;
				if (auto* cs = m_world.try_get<CharAuthorityState>(op.e))
				{
					if (TryReadValidatedCharacterState(m_physics, oid, csBuf))
					{
						cs->state = csBuf;
						spawned = true;
					}
					else
						m_world.remove<PhysicsSpawnedTag>(op.e);
				}
				else if (auto* rs = m_world.try_get<RigidAuthorityState>(op.e))
				{
					if (TryReadValidatedRigidState(m_physics, oid, rsBuf))
					{
						rs->state = rsBuf;
						spawned = true;
					}
					else
						m_world.remove<PhysicsSpawnedTag>(op.e);
				}
				else
				{
					m_world.remove<PhysicsSpawnedTag>(op.e);
				}

				if (spawned && aoi)
					aoi->OnActorSpawned(op.e);
				
				break;
			}

			case PendingActorOp::eType::Despawn:
				if (aoi)
					aoi->OnActorDestroyed(op.e);
				m_world.remove<PhysicsSpawnedTag>(op.e);

				break;
			}
		}
	}

	void ServerPhysicsSystem::HandleProjectileLifecycleEvents() const
	{
		if (!m_physics)
			return;

		auto* nwPtr = m_world.ctx().find<ServerPhysicalWorld*>();
		if (!nwPtr || !*nwPtr)
			return;

		ServerPhysicalWorld* physicalWorld = *nwPtr;
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

			physicalWorld->DespawnActorImmediate(*netId, 0);
		}
	}
}
