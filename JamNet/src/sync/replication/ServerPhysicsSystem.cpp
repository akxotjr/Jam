#include "pch.h"
#include "jamnet/sync/replication/ServerPhysicsSystem.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/ServerInputSystem.h"

namespace jam::net
{
	ServerPhysicsSystem::ServerPhysicsSystem(entt::registry& world, px::IPhysicsFacade* physics)
		: m_world(world), m_physics(physics)
	{
	}

	void ServerPhysicsSystem::Init() const
	{
	}

	void ServerPhysicsSystem::Tick()
	{
		ApplyInputs();
		Simulate();
		SyncTransforms();
	}

	void ServerPhysicsSystem::SpawnActor(entt::entity e, const px::SpawnDesc& desc) const
	{
		if (!m_physics || !m_world.valid(e))
			return;

		const px::ObjectId		id = MakeObjectId(e);
		const px::PhysicsHandle h  = m_physics->Spawn(id, desc);
		if (!h.IsValid()) return;

		const px::eBodyKind kind = m_physics->GetBodyKind(id);
		m_world.emplace<NetActorBodyKind>(e, NetActorBodyKind{ kind });

		if (px::IsCharacterBody(kind))
		{
			m_world.emplace<CharacterPhysicalBody>(e, CharacterPhysicalBody{ h });
			auto& cs = m_world.emplace<px::CharacterState>(e);
			JAM_ASSERT(m_physics->GetCharacterState(id, cs))

			const uint64 controller = m_world.get<ControlTag>(e).userId;
			const uint64 owner		= m_world.get<OwnershipTag>(e).userId;

			if (owner && owner == controller)
			{
				m_world.emplace<px::CharacterInput>(e);
			}
		}
		else
		{
			m_world.emplace<RigidPhysicalBody>(e, RigidPhysicalBody{ h });
			auto& rs = m_world.emplace<px::RigidState>(e);
			JAM_ASSERT(m_physics->GetRigidState(id, rs))
		}
	}

	void ServerPhysicsSystem::DespawnActor(entt::entity e) const
	{
		if (!m_physics || !m_world.valid(e))
			return;

		const px::ObjectId id = MakeObjectId(e);
		m_physics->Despawn(id);

		if (auto* hb = m_world.try_get<CharacterHitboxPhysicalBody>(e))
			hb->handle = {};

		if (auto* cc = m_world.try_get<CharacterPhysicalBody>(e))
			cc->handle = {};

		if (auto* ra = m_world.try_get<RigidPhysicalBody>(e))
			ra->handle = {};
	}

	void ServerPhysicsSystem::ApplyInputs() const
	{
		auto view = m_world.view<ControlTag, px::CharacterInput>();

		for (auto e : view)
		{
			auto& input = view.get<px::CharacterInput>(e);
			m_physics->ApplyCharacterInput(MakeObjectId(e), input);
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
		if (m_physics->BeginStep(SIMULATION_TICK_SEC, awaitKey) && inFiber)
			sched->Suspend(awaitKey, NOW_NS() + 2_s);

		// 파이버가 Resume 된 후 (또는 동기 실행 시) 결과를 가져옵니다.
		m_physics->EndStep();
	}

	void ServerPhysicsSystem::SyncActiveTransforms() const
	{
		if (!m_physics) return;

		// PopActiveList = onAdvance(dynamic) + dirty set(kinematic / character move / setGlobalPose)
		const auto activeList = m_physics->PopActiveList();
		if (activeList.empty()) return;

		for (const px::ObjectId id : activeList)
		{
			// ObjectId == entt::entity (uint32 캐스팅 규칙)
			const entt::entity e = static_cast<entt::entity>(id);
			if (!m_world.valid(e)) continue;

			const auto* kindComp = m_world.try_get<NetActorBodyKind>(e);
			if (!kindComp) continue;

			if (px::IsCharacterBody(kindComp->body))
			{
				px::CharacterState cs{};
				if (m_physics->GetCharacterState(id, cs))
				{
					if (auto* state = m_world.try_get<px::CharacterState>(e))
						*state = cs;

					JAMNET_LOG_TRACE("SyncActiveTransform(Character) id={} pos({:.2f},{:.2f},{:.2f})", id, cs.pos.x, cs.pos.y, cs.pos.z);
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

		auto view = m_world.view<NetIdentity, NetActorBodyKind>();

		for (auto e : view)
		{
			const px::ObjectId id = MakeObjectId(e);
			const auto kind = view.get<NetActorBodyKind>(e).body;

			if (px::IsCharacterBody(kind))
			{
				px::CharacterState cs{};
				if (m_physics->GetCharacterState(id, cs))
				{
					auto& state = m_world.get<px::CharacterState>(e);
					state = cs;

					JAMNET_LOG_TRACE("SyncTransform : pos({}, {}, {})", state.pos.x, state.pos.y, state.pos.z);
				}
			}
			else
			{
				px::RigidState rs{};
				if (m_physics->GetRigidState(id, rs))
				{
					auto& state = m_world.get<px::RigidState>(e);
					state = rs;
				}
			}
		}
	}
}
