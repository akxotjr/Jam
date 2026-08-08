#include "pch.h"
#include "jampx/PhysicsFacade.h"

#include "jampx/PhysicsCompletionTask.h"
#include "jampx/PhysicsWorld.h"
#include "jampx/PhysicsSceneSlot.h"
#include "jampx/ShardPxCpuDispatcher.h"
#include "jampx/actor/ActorFactory.h"
#include "jampx/actor/character/CharacterBody.h"
#include "jampx/actor/rigid/RigidBody.h"
#include "jampx/actor/rigid/projectile/ProjectileRigidBehavior.h"
#include "jampx/prefab/PhysicsArchetypeRegistry.h"

#include <algorithm>
#include <atomic>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

namespace jam::px
{
	struct PhysicsFacade::Impl
	{
		enum class ePendingSceneOpType
		{
			Spawn,
			Despawn,
		};

		struct PendingSceneOp
		{
			ePendingSceneOpType type = ePendingSceneOpType::Spawn;
			ActorId				id	 = INVALID_ACTOR_ID;
			SpawnDesc			desc = {};
		};

		std::atomic<bool>							m_inited = false;
		std::unique_ptr<PhysicsWorld>				m_world;
		PhysicsArchetypeRegistry					m_registry;
		PxTaskManager*								m_taskManager = nullptr;
		IPhysicsJobBridge*							m_bridge = nullptr;
		std::string									m_physicsAssetPath;
		std::unique_ptr<ShardPxCpuDispatcher>		m_dispacter;
		PhysicsCompletionTask						m_completionTask;
		bool										m_stepPending = false;
		std::vector<PendingSceneOp>					m_pendingSceneOps;
		std::unordered_map<ActorId, RigidBody>		m_rigidMap;
		std::unordered_map<ActorId, RigidBody>		m_kinematicMap;
		std::unordered_map<ActorId, RigidBody>		m_projectileMap;
		std::unordered_map<ActorId, CharacterBody>	m_cctMap;
		std::unordered_map<ActorId, CharacterBody>	m_remoteCctMap;
		std::vector<SimEvent>						m_pendingSimEvents;
		std::unordered_set<ActorId>					m_dirtySet;

		std::optional<PxTransform> ResolveTargetPose(ActorId oid);
	};

	PhysicsFacade::PhysicsFacade()
		: m_impl(std::make_unique<Impl>())
	{
	}

	PhysicsFacade::~PhysicsFacade()
	{
		Shutdown();
	}

	void PhysicsFacade::Init()
	{
		if (m_impl->m_inited.load(std::memory_order_relaxed)) return;
		if (m_impl->m_physicsAssetPath.empty())
			throw std::runtime_error("PhysicsFacade requires physics asset path before Init()");

		m_impl->m_registry.Init(m_impl->m_physicsAssetPath);

		m_impl->m_world = std::make_unique<PhysicsWorld>();
		m_impl->m_world->Init(&m_impl->m_registry, m_impl->m_dispacter.get());

		m_impl->m_taskManager = m_impl->m_world->GetTaskManager();

		m_impl->m_inited.store(true, std::memory_order_relaxed);
	}

	void PhysicsFacade::Shutdown()
	{
		if (!m_impl->m_inited.load(std::memory_order_relaxed)) return;

		JAM_ASSERT_MSG(!m_impl->m_stepPending, "PhysicsFacade cannot shut down while a simulation step is pending");

		if (m_impl->m_world)
		{
			for (const auto& body : m_impl->m_rigidMap | std::views::values)
				ActorFactory::DestroyRigidBody(*m_impl->m_world, body);
			for (const auto& body : m_impl->m_kinematicMap | std::views::values)
				ActorFactory::DestroyRigidBody(*m_impl->m_world, body);
			for (const auto& body : m_impl->m_projectileMap | std::views::values)
				ActorFactory::DestroyRigidBody(*m_impl->m_world, body);
			for (const auto& body : m_impl->m_cctMap | std::views::values)
				ActorFactory::DestroyCharacterBody(*m_impl->m_world, body);
			for (const auto& body : m_impl->m_remoteCctMap | std::views::values)
				ActorFactory::DestroyCharacterBody(*m_impl->m_world, body);
		}

		m_impl->m_rigidMap.clear();
		m_impl->m_kinematicMap.clear();
		m_impl->m_projectileMap.clear();
		m_impl->m_cctMap.clear();
		m_impl->m_remoteCctMap.clear();
		m_impl->m_pendingSceneOps.clear();
		m_impl->m_pendingSimEvents.clear();
		m_impl->m_dirtySet.clear();

		if (m_impl->m_world) m_impl->m_world->Destroy();
		m_impl->m_world.reset();
		m_impl->m_registry.Shutdown();
		m_impl->m_taskManager = nullptr;
		m_impl->m_stepPending = false;
		m_impl->m_dispacter.reset();
		m_impl->m_bridge = nullptr;

		m_impl->m_inited.store(false, std::memory_order_relaxed);
	}

	void PhysicsFacade::SetJobBridge(IPhysicsJobBridge* bridge)
	{
		JAM_ASSERT(!m_impl->m_inited.load());
		m_impl->m_bridge = bridge;
		if (bridge)
			m_impl->m_dispacter = std::make_unique<ShardPxCpuDispatcher>(*bridge);
	}

	void PhysicsFacade::SetPhysicsAssetPath(const std::string& path)
	{
		JAM_ASSERT(!m_impl->m_inited.load());
		m_impl->m_physicsAssetPath = path;
	}

	bool PhysicsFacade::IsStepPending() const
	{
		return m_impl->m_stepPending;
	}

	void PhysicsFacade::Simulate(float dt)
	{
		if (!m_impl->m_inited.load(std::memory_order_relaxed) || !m_impl->m_world) return;

		StepCharacters(ePxSceneSlot::Main, dt);
		StepKinematics(ePxSceneSlot::Main, dt);
		StepProjectiles(ePxSceneSlot::Main, dt);
		m_impl->m_world->Simulate(ePxSceneSlot::Main, dt);
	}

	bool PhysicsFacade::BeginSimulate(float dt, uint64 awaitKey)
	{
		if (!m_impl->m_world) return false;

		StepCharacters(ePxSceneSlot::Main, dt);
		StepKinematics(ePxSceneSlot::Main, dt);
		StepProjectiles(ePxSceneSlot::Main, dt);

		if (!m_impl->m_dispacter || awaitKey == 0)
		{
			m_impl->m_world->Simulate(ePxSceneSlot::Main, dt);
			return false;
		}

		m_impl->m_completionTask.SetPhysicsJobBridge(m_impl->m_bridge);
		m_impl->m_completionTask.SetAwaitKey(awaitKey);
		m_impl->m_completionTask.setContinuation(*m_impl->m_taskManager, nullptr);

		m_impl->m_world->BeginSimulate(ePxSceneSlot::Main, dt, &m_impl->m_completionTask);

		m_impl->m_completionTask.removeReference();

		m_impl->m_stepPending = true;
		return true; // Suspend 해야 함을 알림
	}

	void PhysicsFacade::EndSimulate()
	{
		if (!m_impl->m_world || !m_impl->m_stepPending) return;
		m_impl->m_world->EndSimulate(ePxSceneSlot::Main);
		m_impl->m_stepPending = false;

		FlushPendingSceneOps();

		SyncKinematics(ePxSceneSlot::Main);
		SyncProjectiles(ePxSceneSlot::Main);
	}

	void PhysicsFacade::Resimulate(float dt)
	{
		if (!m_impl->m_inited.load(std::memory_order_relaxed) || !m_impl->m_world) return;

		StepCharacters(ePxSceneSlot::Replay, dt);
		StepKinematics(ePxSceneSlot::Replay, dt);
		StepProjectiles(ePxSceneSlot::Replay, dt);

		m_impl->m_world->Simulate(ePxSceneSlot::Replay, dt);
	}

	bool PhysicsFacade::BeginResimulate(float dt, uint64 awaitKey)
	{
		if (!m_impl->m_world) return false;

		StepCharacters(ePxSceneSlot::Replay, dt);
		StepKinematics(ePxSceneSlot::Replay, dt);
		StepProjectiles(ePxSceneSlot::Replay, dt);

		if (!m_impl->m_dispacter || awaitKey == 0)
		{
			m_impl->m_world->Simulate(ePxSceneSlot::Replay, dt);
			return false;
		}

		m_impl->m_completionTask.SetPhysicsJobBridge(m_impl->m_bridge);
		m_impl->m_completionTask.SetAwaitKey(awaitKey);
		m_impl->m_completionTask.setContinuation(*m_impl->m_taskManager, nullptr);

		m_impl->m_world->BeginSimulate(ePxSceneSlot::Replay, dt, &m_impl->m_completionTask);

		m_impl->m_completionTask.removeReference();

		m_impl->m_stepPending = true;
		return true;
	}

	void PhysicsFacade::EndResimulate()
	{
		if (!m_impl->m_world || !m_impl->m_stepPending) return;
		m_impl->m_world->EndSimulate(ePxSceneSlot::Replay);
		m_impl->m_stepPending = false;

		FlushPendingSceneOps();

		SyncKinematics(ePxSceneSlot::Replay);
		SyncProjectiles(ePxSceneSlot::Replay);
	}

	bool PhysicsFacade::Spawn(ActorId id, const SpawnDesc& desc)
	{
		if (!m_impl->m_inited.load(std::memory_order_relaxed) || !m_impl->m_world) return false;

		if (m_impl->m_stepPending)
		{
			m_impl->m_pendingSceneOps.push_back(Impl::PendingSceneOp{
				.type = Impl::ePendingSceneOpType::Spawn,
				.id   = id, 
				.desc = desc });
			return true;
		}

		return SpawnNow(id, desc);
	}

	bool PhysicsFacade::Despawn(ActorId id)
	{
		if (!m_impl->m_inited.load(std::memory_order_relaxed) || !m_impl->m_world) return false;

		if (m_impl->m_stepPending)
		{
			m_impl->m_pendingSceneOps.push_back(Impl::PendingSceneOp{
				.type = Impl::ePendingSceneOpType::Despawn,
				.id   = id });
			return true;
		}

		return DespawnNow(id);
	}

	eBodyType PhysicsFacade::GetBodyType(ActorId id) const
	{
		if (m_impl->m_rigidMap.contains(id))		return eBodyType::Rigid;
		if (m_impl->m_kinematicMap.contains(id))	return eBodyType::Rigid;
		if (m_impl->m_projectileMap.contains(id))	return eBodyType::Rigid;
		if (m_impl->m_cctMap.contains(id))			return eBodyType::Character;
		if (m_impl->m_remoteCctMap.contains(id))	return eBodyType::Character;
		return eBodyType::None;
	}

	eBodyType PhysicsFacade::FindBodyType(PhysicsArchetypeKey key) const
	{
		return m_impl->m_registry.GetBodyType(key);
	}


	eMotionType PhysicsFacade::GetMotionType(ActorId id) const
	{
		if (m_impl->m_rigidMap.contains(id))
		{
			const auto* actor = m_impl->m_rigidMap.at(id).GetMainActor();
			if (!actor) return eMotionType::None;
			if (actor->is<PxRigidStatic>()) return eMotionType::Static;
			return actor->is<PxRigidDynamic>() ? eMotionType::Dynamic : eMotionType::None;
		}

		if (m_impl->m_kinematicMap.contains(id))	return eMotionType::Kinematic;
		if (m_impl->m_projectileMap.contains(id))	return eMotionType::Kinematic;
		if (m_impl->m_cctMap.contains(id))			return eMotionType::CCT;
		if (m_impl->m_remoteCctMap.contains(id))	return eMotionType::RemoteCCT;

		return eMotionType::None;
	}

	eMotionType PhysicsFacade::FindMotionType(PhysicsArchetypeKey key) const
	{
		return m_impl->m_registry.GetMotionType(key);
	}

	bool PhysicsFacade::IsReplayCandidate(PhysicsArchetypeKey key) const
	{
		const auto* def = m_impl->m_registry.FindArchetype(key);
		if (!def) return false;

		if (def->bodyType == eBodyType::Character)
			return true;

		if (def->bodyType != eBodyType::Rigid || !def->IsRigid())
			return false;

		const auto& rigidDef = std::get<RigidBodyData>(def->body);
		for (ShapeHandle sh : rigidDef.shapes)
		{
			const ShapeData& sd = m_impl->m_registry.GetShapeDef(sh);

			if (sd.simFD.category.has_any(SimCategory::CHARACTER))		return true;
			if (sd.simFD.mask.has_any(SimCategory::CHARACTER))			return true;
			if (sd.qryFD.category.has_any(QueryCategory::CHARACTER))	return true;
		}

		return false;
	}

	void PhysicsFacade::PushReplayStates(const std::vector<ActorContext>& contexts)
	{
		for (const auto& c : contexts)
		{
			if (const auto* rs = std::get_if<RigidState>(&c.state))
			{
				if (auto it = m_impl->m_rigidMap.find(c.actorId);	   it != m_impl->m_rigidMap.end())		{ it->second.ApplyReplayState(*rs, false); continue; }
				if (auto it = m_impl->m_kinematicMap.find(c.actorId);  it != m_impl->m_kinematicMap.end())	{ it->second.ApplyReplayState(*rs, true);  continue; }
				if (auto it = m_impl->m_projectileMap.find(c.actorId); it != m_impl->m_projectileMap.end()) { it->second.ApplyReplayState(*rs, true);  continue; }
				continue;
			}

			if (const auto* cs = std::get_if<CharacterState>(&c.state))
			{
				if (auto it = m_impl->m_cctMap.find(c.actorId);		  it != m_impl->m_cctMap.end())		  { it->second.ApplyAuthorityToReplay(*cs); continue; }
				if (auto it = m_impl->m_remoteCctMap.find(c.actorId); it != m_impl->m_remoteCctMap.end()) { it->second.ApplyAuthorityToReplay(*cs); continue; }
			}
		}
	}
	
	void PhysicsFacade::PullCorrectionState(ActorId oid, OUT CharacterState& state)
	{
		if (auto it = m_impl->m_cctMap.find(oid); it != m_impl->m_cctMap.end())
		{
			state = it->second.GetReplayState();
		}
	}

	void PhysicsFacade::PullPredictedState(ActorId id, OUT CharacterState& state)
	{
		if (auto it = m_impl->m_cctMap.find(id); it != m_impl->m_cctMap.end())
		{
			state = it->second.GetMainState();
		}
	}

	void PhysicsFacade::PushAuthorityStates(const std::vector<ActorContext>& contexts)
	{
		for (const auto& c : contexts)
		{
			if (const auto* rs = std::get_if<RigidState>(&c.state))
			{
				if (auto it = m_impl->m_rigidMap.find(c.actorId);	   it != m_impl->m_rigidMap.end())		{ it->second.ApplyMainState(*rs, false); MarkDirty(c.actorId); continue; }
				if (auto it = m_impl->m_kinematicMap.find(c.actorId);  it != m_impl->m_kinematicMap.end())	{ it->second.ApplyMainState(*rs, true);  MarkDirty(c.actorId); continue; }
				if (auto it = m_impl->m_projectileMap.find(c.actorId); it != m_impl->m_projectileMap.end()) { it->second.ApplyMainState(*rs, true);  MarkDirty(c.actorId); continue; }
				continue;
			}

			if (const auto* cs = std::get_if<CharacterState>(&c.state))
			{
				if (auto it = m_impl->m_cctMap.find(c.actorId);		  it != m_impl->m_cctMap.end())		  { it->second.ApplyAuthorityToMain(*cs); MarkDirty(c.actorId); continue; }
				if (auto it = m_impl->m_remoteCctMap.find(c.actorId); it != m_impl->m_remoteCctMap.end()) { it->second.ApplyAuthorityToMain(*cs); MarkDirty(c.actorId); continue; }
			}
		}
	}

	void PhysicsFacade::PullProxyStates(OUT std::vector<ActorContext>& contexts)
	{
		auto fillOne = [this](ActorContext& c)
			{
				if (auto it = m_impl->m_rigidMap.find(c.actorId);	   it != m_impl->m_rigidMap.end())		{ c.state = it->second.GetMainState(); return true; }
				if (auto it = m_impl->m_kinematicMap.find(c.actorId);  it != m_impl->m_kinematicMap.end())	{ c.state = it->second.GetMainState(); return true; }
				if (auto it = m_impl->m_projectileMap.find(c.actorId); it != m_impl->m_projectileMap.end()) { c.state = it->second.GetMainState(); return true; }
				if (auto it = m_impl->m_cctMap.find(c.actorId);		   it != m_impl->m_cctMap.end())		{ c.state = it->second.GetMainState(); return true; }
				if (auto it = m_impl->m_remoteCctMap.find(c.actorId);  it != m_impl->m_remoteCctMap.end())	{ c.state = it->second.GetMainState(); return true; }
				return false;
			};

		if (contexts.empty())
		{
			contexts.reserve(m_impl->m_rigidMap.size() + m_impl->m_kinematicMap.size() + m_impl->m_projectileMap.size() + m_impl->m_cctMap.size() + m_impl->m_remoteCctMap.size());

			for (const auto& [id, b] : m_impl->m_rigidMap)		{ ActorContext c{}; c.actorId = id; c.state = b.GetMainState(); contexts.push_back(c); }
			for (const auto& [id, b] : m_impl->m_kinematicMap)	{ ActorContext c{}; c.actorId = id; c.state = b.GetMainState(); contexts.push_back(c); }
			for (const auto& [id, b] : m_impl->m_projectileMap) { ActorContext c{}; c.actorId = id; c.state = b.GetMainState(); contexts.push_back(c); }
			for (const auto& [id, b] : m_impl->m_cctMap)		{ ActorContext c{}; c.actorId = id; c.state = b.GetMainState(); contexts.push_back(c); }
			for (const auto& [id, b] : m_impl->m_remoteCctMap)	{ ActorContext c{}; c.actorId = id; c.state = b.GetMainState(); contexts.push_back(c); }
			return;
		}

		for (auto& c : contexts)
			fillOne(c);
	}


	bool PhysicsFacade::GetCharacterState(ActorId id, CharacterState& state) const
	{
		if (auto it = m_impl->m_cctMap.find(id); it != m_impl->m_cctMap.end())
		{
			state = it->second.GetMainState();
			return true;
		}
		if (auto it = m_impl->m_remoteCctMap.find(id); it != m_impl->m_remoteCctMap.end())
		{
			state = it->second.GetMainState();
			return true;
		}
		return false;
	}

	bool PhysicsFacade::SetCharacterState(ActorId id, const CharacterState& state)
	{
		if (auto it = m_impl->m_cctMap.find(id); it != m_impl->m_cctMap.end())
		{
			it->second.ApplyAuthorityToMain(state);
			MarkDirty(id);
			return true;
		}
		if (auto it = m_impl->m_remoteCctMap.find(id); it != m_impl->m_remoteCctMap.end())
		{
			it->second.ApplyAuthorityToMain(state);
			MarkDirty(id);
			return true;
		}
		return false;
	}

	bool PhysicsFacade::GetRigidState(ActorId id, RigidState& state) const
	{
		if (auto it = m_impl->m_rigidMap.find(id); it != m_impl->m_rigidMap.end())
		{
			state = it->second.GetMainState();
			return true;
		}
		if (auto it = m_impl->m_kinematicMap.find(id); it != m_impl->m_kinematicMap.end())
		{
			state = it->second.GetMainState();
			return true;
		}
		if (auto it = m_impl->m_projectileMap.find(id); it != m_impl->m_projectileMap.end())
		{
			state = it->second.GetMainState();
			return true;
		}

		return false;
	}

	bool PhysicsFacade::SetRigidState(ActorId id, const RigidState& state)
	{
		if (auto it = m_impl->m_rigidMap.find(id); it != m_impl->m_rigidMap.end())
		{
			it->second.ApplyMainState(state, false);
			return true;
		}
		if (auto it = m_impl->m_kinematicMap.find(id); it != m_impl->m_kinematicMap.end())
		{
			it->second.ApplyMainState(state, true);
			return true;
		}
		if (auto it = m_impl->m_projectileMap.find(id); it != m_impl->m_projectileMap.end())
		{
			it->second.ApplyMainState(state, true);
			return true;
		}
		return false;
	}


	void PhysicsFacade::ApplyCharacterMotorInput(ActorId id, const CharacterMotorInput& input)
	{
		auto it = m_impl->m_cctMap.find(id);
		if (it == m_impl->m_cctMap.end()) return;

		CharacterBody& body = it->second;
		body.SetPlayerInput(input);
	}

	void PhysicsFacade::ApplyReplayCharacterMotorInput(ActorId id, const CharacterMotorInput& input)
	{
		auto it = m_impl->m_cctMap.find(id);
		if (it == m_impl->m_cctMap.end()) return;

		CharacterBody& body = it->second;
		body.SetReplayInput(input);
	}

	bool PhysicsFacade::RaycastLOS(const Vec3& from, const Vec3& to) const
	{
		if (!m_impl->m_world) return true;
		PxScene* scene = m_impl->m_world->GetScene(ePxSceneSlot::Main);
		if (!scene) return true;

		const PxVec3 origin = ToPhysX(from);
		PxVec3 dir = ToPhysX(to - from);
		const float  dist = dir.magnitude();
		if (dist < 1e-3f) return true;
		dir /= dist;

		RequestQueryFD reqFD = MakeRequestQueryFD(
			QueryCategory::WORLD,
			0, QuerySublayer::LOS, 0,
			0,
			RequestQueryFlag::IGNORE_TRIGGERS
		);

		QueryFilterCallbackT<> cb{ DefaultQueryPolicy{}, k_LOSQueryHitTypeMap };

		const PxQueryFilterData fd = MakePxQueryFilterData(reqFD);

		PxRaycastBuffer buf;
		return !scene->raycast(origin, dir, dist, buf, PxHitFlag::eDEFAULT, fd, &cb);
	}


	HitscanResult PhysicsFacade::Hitscan(const Vec3& from, const Vec3& dir, float maxRange) const
	{
		HitscanResult result{};
		if (!m_impl->m_world) return result;

		PxScene* scene = m_impl->m_world->GetScene(ePxSceneSlot::Main);
		if (!scene) return result;

		const PxVec3 origin = ToPhysX(from);
		const PxVec3 pxDir = ToPhysX(dir.GetNormalized());

		RequestQueryFD reqFD = MakeRequestQueryFD(
			QueryCategory::CHARACTER | QueryCategory::HITBOX | QueryCategory::WORLD,
			0, QuerySublayer::Default, 0,
			0,
			RequestQueryFlag::IGNORE_TRIGGERS
		);
		QueryFilterCallbackT<> cb{ DefaultQueryPolicy{}, k_LOSQueryHitTypeMap };
		const PxQueryFilterData fd = MakePxQueryFilterData(reqFD);

		PxRaycastBuffer buf;
		if (!scene->raycast(origin, pxDir, maxRange, buf, PxHitFlag::eDEFAULT, fd, &cb))
			return result;

		result.hit      = true;
		result.position = ToPx(buf.block.position);
		result.normal   = ToPx(buf.block.normal);
		result.hitActorId    = GetActorId(buf.block.actor);

		return result;
	}

	std::vector<PhysicsEvent> PhysicsFacade::ConsumePhysicsEvents()
	{
		if (!m_impl->m_world) return {};

		auto simEvents = m_impl->m_world->ConsumeSimEvents();

		// ANALYTIC 투사체 히트 등 수동 push 이벤트 병합
		if (!m_impl->m_pendingSimEvents.empty())
		{
			simEvents.insert(simEvents.end(), m_impl->m_pendingSimEvents.begin(), m_impl->m_pendingSimEvents.end());
			m_impl->m_pendingSimEvents.clear();
		}

		std::vector<PhysicsEvent> out;
		out.reserve(simEvents.size());

		for (const SimEvent& e : simEvents)
		{
			PhysicsEvent evt{};

			if (e.type == eSimEventType::ProjectileHit)
			{
				evt.type = ePhysicsEventType::ProjectileHit;
				evt.sourceActorId = e.contact0;
				evt.targetActorId = e.contact1;

				if (e.contactPointCount > 0)
				{
					evt.hitPosition = ToPx(e.contactPoints[0].position);
					evt.hitNormal = ToPx(e.contactPoints[0].normal);
				}

				out.push_back(evt);
			}
			else if (e.type == eSimEventType::ProjectileLifetimeExpired)
			{
				evt.type = ePhysicsEventType::ProjectileLifetimeExpired;
				evt.sourceActorId = e.contact0;
				out.push_back(evt);
			}
			else if (e.type == eSimEventType::TriggerFound
				|| e.type == eSimEventType::TriggerLost)
			{
				evt.type = e.type == eSimEventType::TriggerFound
					? ePhysicsEventType::TriggerFound
					: ePhysicsEventType::TriggerLost;
				evt.triggerActorId = e.trigger0;
				evt.otherActorId = e.trigger1;
				out.push_back(evt);
			}
		}

		return out;
	}

	std::vector<ActorId> PhysicsFacade::PopActiveList()
	{
		if (!m_impl->m_world) return {};

		auto list = m_impl->m_world->ConsumeAdvancdActive();

		list.reserve(list.size() + m_impl->m_dirtySet.size());
		for (const ActorId id : m_impl->m_dirtySet)
			list.push_back(id);

		m_impl->m_dirtySet.clear();

		std::ranges::sort(list);
		list.erase(std::ranges::unique(list).begin(), list.end());

		return list;
	}

	void PhysicsFacade::MarkDirty(ActorId id)
	{
		m_impl->m_dirtySet.insert(id);
	}

	void PhysicsFacade::FlushPendingSceneOps()
	{
		if (!m_impl->m_world || m_impl->m_pendingSceneOps.empty())
			return;

		auto pending = std::move(m_impl->m_pendingSceneOps);
		m_impl->m_pendingSceneOps.clear();

		for (const auto& op : pending)
		{
			if (op.type == Impl::ePendingSceneOpType::Spawn)
				SpawnNow(op.id, op.desc);
			else
				DespawnNow(op.id);
		}
	}

	bool PhysicsFacade::SpawnNow(ActorId id, const SpawnDesc& desc)
	{
		if (!m_impl->m_inited.load(std::memory_order_relaxed) || !m_impl->m_world)
			return false;

		const PhysicsArchetypeKey archetypeKey = desc.archetype;
		const PhysicsArchetypeData* tplDef = m_impl->m_registry.FindArchetype(archetypeKey);
		if (!tplDef) return false;

		const auto actorType  = tplDef->actorType;
		const auto bodyType   = tplDef->bodyType;
		const auto motionType = tplDef->motionType;

		if (bodyType == eBodyType::Rigid)
		{
			auto resolver = [this](ActorId oid) -> std::optional<PxTransform>
				{
					return m_impl->ResolveTargetPose(oid);
				};

			auto body = ActorFactory::CreateRigidBody(*m_impl->m_world, archetypeKey, *tplDef, desc, id, resolver);
			if (!body.has_value()) return false;

			switch (motionType)
			{
			case eMotionType::Static:
			case eMotionType::Dynamic:
				m_impl->m_rigidMap.emplace(id, std::move(body.value()));
				return true;

			case eMotionType::Kinematic:
				if (actorType == eActorType::Projectile)
					m_impl->m_projectileMap.emplace(id, std::move(body.value()));
				else
					m_impl->m_kinematicMap.emplace(id, std::move(body.value()));
				return true;

			default:
				break;
			}
		}
		else if (bodyType == eBodyType::Character)
		{
			auto body = ActorFactory::CreateCharacterBody(*m_impl->m_world, archetypeKey, *tplDef, desc, id);
			if (!body.has_value()) return false;

			if (motionType == eMotionType::CCT)
			{
				m_impl->m_cctMap.emplace(id, std::move(body.value()));
				return true;
			}
			if (motionType == eMotionType::RemoteCCT)
			{
				m_impl->m_remoteCctMap.emplace(id, std::move(body.value()));
				return true;
			}
		}

		JAM_ASSERT_MSG(false, "Unknown body type in archetype: {}", tplDef->name);
		return false;
	}

	bool PhysicsFacade::DespawnNow(ActorId id)
	{
		if (!m_impl->m_world) return false;

		if (auto it = m_impl->m_rigidMap.find(id); it != m_impl->m_rigidMap.end())
		{
			ActorFactory::DestroyRigidBody(*m_impl->m_world, it->second);
			m_impl->m_rigidMap.erase(it);
			return true;
		}
		if (auto it = m_impl->m_kinematicMap.find(id); it != m_impl->m_kinematicMap.end())
		{
			ActorFactory::DestroyRigidBody(*m_impl->m_world, it->second);
			m_impl->m_kinematicMap.erase(it);
			return true;
		}
		if (auto it = m_impl->m_projectileMap.find(id); it != m_impl->m_projectileMap.end())
		{
			ActorFactory::DestroyRigidBody(*m_impl->m_world, it->second);
			m_impl->m_projectileMap.erase(it);
			return true;
		}
		if (auto it = m_impl->m_cctMap.find(id); it != m_impl->m_cctMap.end())
		{
			ActorFactory::DestroyCharacterBody(*m_impl->m_world, it->second);
			m_impl->m_cctMap.erase(it);
			return true;
		}
		if (auto it = m_impl->m_remoteCctMap.find(id); it != m_impl->m_remoteCctMap.end())
		{
			ActorFactory::DestroyCharacterBody(*m_impl->m_world, it->second);
			m_impl->m_remoteCctMap.erase(it);
			return true;
		}

		return false;
	}

	void PhysicsFacade::StepCharacters(ePxSceneSlot slot, float dt)
	{
		for (auto& [id, body] : m_impl->m_cctMap)
		{
			if (slot == ePxSceneSlot::Main)
			{
				if (body.TickOnMain(dt))
					MarkDirty(id);
			}
			else if (slot == ePxSceneSlot::Replay)
			{
				body.TickOnReplay(dt);
			}
			
		}
	}

	void PhysicsFacade::StepKinematics(ePxSceneSlot slot, float dt)
	{
		for (auto& body : m_impl->m_kinematicMap | std::views::values)
		{
			if (slot == ePxSceneSlot::Main)
				body.TickOnMain(dt);
			else if (slot == ePxSceneSlot::Replay)
				body.TickOnReplay(dt);
		}
	}

	void PhysicsFacade::StepProjectiles(ePxSceneSlot slot, float dt)
	{
		if (m_impl->m_projectileMap.empty()) return;

		for (auto& [id, body] : m_impl->m_projectileMap)
		{
			if (slot == ePxSceneSlot::Main)
			{
				body.TickOnMain(dt);

				auto* proj = dynamic_cast<ProjectileRigidBehavior*>(body.GetBehavior());
				if (!proj) continue;

				ProjectileHitResult r{};
				if (proj->ConsumeMainHitEvent(r))
				{
					SimEvent e{};
					e.type = eSimEventType::ProjectileHit;
					e.contact0					= id;
					e.contact1					= r.hitActorId;
					e.contactPointCount			= 1;
					e.contactPoints[0].position = r.position;
					e.contactPoints[0].normal	= r.normal;
					m_impl->m_pendingSimEvents.push_back(e);
				}

				if (proj->ConsumeMainLifetimeEvent(r))
				{
					SimEvent e{};
					e.type = eSimEventType::ProjectileLifetimeExpired;
					e.contact0 = id;
					m_impl->m_pendingSimEvents.push_back(e);
				}
			}
			else if (slot == ePxSceneSlot::Replay)
			{
				body.TickOnReplay(dt);
			}
		}
	}

	void PhysicsFacade::SyncKinematics(ePxSceneSlot slot)
	{
		for (auto& [id, body] : m_impl->m_kinematicMap)
		{
			if (slot == ePxSceneSlot::Main)
			{
				if (body.SyncMainState(body))
					MarkDirty(id);
			}
			else if (slot == ePxSceneSlot::Replay)
			{
				body.SyncReplayState(body);
			}
		}
	}

	void PhysicsFacade::SyncProjectiles(ePxSceneSlot slot)
	{
		for (auto& [id, body] : m_impl->m_projectileMap)
		{
			if (slot == ePxSceneSlot::Main)
			{
				if (body.SyncMainState(body))
					MarkDirty(id);
			}
			else if (slot == ePxSceneSlot::Replay)
			{
				body.SyncReplayState(body);
			}
		}
	}

	std::optional<PxTransform> PhysicsFacade::Impl::ResolveTargetPose(ActorId oid)
	{
		if (auto it = m_rigidMap.find(oid); it != m_rigidMap.end())
			return ToPhysX(it->second.GetMainState().pose);

		if (auto it = m_kinematicMap.find(oid); it != m_kinematicMap.end())
			return ToPhysX(it->second.GetMainState().pose);

		if (auto it = m_projectileMap.find(oid); it != m_projectileMap.end())
			return ToPhysX(it->second.GetMainState().pose);

		if (auto it = m_cctMap.find(oid); it != m_cctMap.end())
			return PxTransform(ToPhysX(it->second.GetMainState().pos));

		if (auto it = m_remoteCctMap.find(oid); it != m_remoteCctMap.end())
			return PxTransform(ToPhysX(it->second.GetMainState().pos));

		return std::nullopt;
	}
}
