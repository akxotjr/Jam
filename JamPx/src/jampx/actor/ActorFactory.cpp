#include "pch.h"
#include "jampx/actor/ActorFactory.h"
#include "jampx/actor/character/controller/AIControllerComponent.h"
#include "jampx/actor/character/controller/PlayerControllerComponent.h"
#include "jampx/actor/rigid/kinematic/IKinematicDriver.h"
#include "jampx/actor/rigid/kinematic/KinematicCommon.h"
#include "jampx/actor/rigid/kinematic/KinematicDrivers.h"
#include "jampx/actor/rigid/kinematic/KinematicRigidBehavior.h"
#include "jampx/actor/rigid/projectile/ProjectileRigidBehavior.h"
#include "jampx/prefab/PhysicsArchetypeRegistry.h"

namespace jam::px
{
	namespace
	{
		void ApplyRigidOverrides(PxRigidDynamic& dyn, const RigidSpawnOverrides& overrides)
		{
			if (dyn.getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC))
				return;

			if (overrides.mask.has_any(SpawnOverrideMask::LINEAR_VEL))
				dyn.setLinearVelocity(ToPhysX(overrides.linearVelocity));
			if (overrides.mask.has_any(SpawnOverrideMask::ANGULAR_VEL))
				dyn.setAngularVelocity(ToPhysX(overrides.angularVelocity));
			if (overrides.mask.has_any(SpawnOverrideMask::LINEAR_DAMP))
				dyn.setLinearDamping(overrides.linearDamping);
			if (overrides.mask.has_any(SpawnOverrideMask::ANGULAR_DAMP))
				dyn.setAngularDamping(overrides.angularDamping);
		}

		void ApplyRigidOverridesToActor(PxRigidActor* actor, const RigidSpawnOverrides& overrides)
		{
			if (!actor) return;
			if (auto* dyn = actor->is<PxRigidDynamic>())
				ApplyRigidOverrides(*dyn, overrides);
		}

		eMotionType ResolveMotionType(eSpawnSource spawnSource, const PhysicsArchetypeData& data)
		{
			if (spawnSource != eSpawnSource::Network)
				return data.motionType;

			// Network + Rigid(Dynamic/Kinematic) -> Kinematic(NetworkPose 추종)
			if (data.bodyType == eBodyType::Rigid
				&& (data.motionType == eMotionType::Dynamic || data.motionType == eMotionType::Kinematic))
			{
				return eMotionType::Kinematic;
			}

			// Network + Character(CCT) -> RemoteCCT
			if (data.bodyType == eBodyType::Character && data.motionType == eMotionType::CCT)
			{
				return eMotionType::RemoteCCT;
			}

			return data.motionType;
		}

		std::unique_ptr<IKinematicDriver> CreateKinematicDriver(const KinematicDriverConfig& cfg, const TargetPoseResolver& resolver)
		{
			if (const auto* src = std::get_if<WaypointSource>(&cfg.source))
				return std::make_unique<WaypointKinematicDriver>(cfg.common, *src);

			if (const auto* src = std::get_if<CurveSource>(&cfg.source))
				return std::make_unique<CurveKinematicDriver>(cfg.common, *src);

			if (const auto* src = std::get_if<OrbitSource>(&cfg.source))
				return std::make_unique<OrbitKinematicDriver>(cfg.common, *src, resolver);

			if (const auto* src = std::get_if<FollowSource>(&cfg.source))
				return std::make_unique<FollowKinematicDriver>(cfg.common, *src, resolver);

			if (const auto* src = std::get_if<NetworkPoseSource>(&cfg.source))
				return std::make_unique<NetworkPoseKinematicDriver>(cfg.common, *src);

			return nullptr;
		}

		std::unique_ptr<IRigidBehavior> CreateRigidBehavior(
			const PhysicsArchetypeRegistry& registry,
			const PhysicsArchetypeData& data, 
			const TargetPoseResolver& resolver,
			const RigidSpawnOverrides* overrides)
		{
			if (!data.IsRigid())
				return nullptr;

			const auto& bodyDef = std::get<RigidBodyData>(data.body);
			if (!bodyDef.HasBehavior())
				return nullptr;

			if (const auto* kHandle = bodyDef.GetBehavior<KinematicDriverConfigHandle>())
			{
				const KinematicDriverConfig& cfg = registry.GetKinematicDriverConfig(*kHandle);

				auto mainDriver   = CreateKinematicDriver(cfg, resolver);
				auto replayDriver = CreateKinematicDriver(cfg, resolver);

				if (mainDriver && replayDriver)
					return std::make_unique<KinematicRigidBehavior>(std::move(mainDriver), std::move(replayDriver));
			}
			else if (const auto* pHandle = bodyDef.GetBehavior<ProjectileConfigHandle>())
			{
				ProjectileConfig cfg = registry.GetProjectileConfig(*pHandle);

				if (overrides && overrides->mask.has_any(SpawnOverrideMask::LINEAR_VEL))
					cfg.motion.initialVelocity = ToPhysX(overrides->linearVelocity);

				auto mainProjectile   = std::make_unique<ProjectileComponent>(cfg);
				auto replayProjectile = std::make_unique<ProjectileComponent>(cfg);

				return std::make_unique<ProjectileRigidBehavior>(std::move(mainProjectile), std::move(replayProjectile));
			}

			return nullptr;
		}

		eKineDrivenType ResolveKineDrivenType(const PhysicsArchetypeRegistry& registry, const PhysicsArchetypeData& data)
		{
			if (!data.IsRigid())
				return eKineDrivenType::None;

			const auto& bodyDef = std::get<RigidBodyData>(data.body);
			if (!bodyDef.HasBehavior())
				return eKineDrivenType::None;

			if (const auto* kHandle = bodyDef.GetBehavior<KinematicDriverConfigHandle>())
			{
				const KinematicDriverConfig& cfg = registry.GetKinematicDriverConfig(*kHandle);
				const PoseSource& src = cfg.source;

				if (std::holds_alternative<WaypointSource>(src))
					return eKineDrivenType::Deterministic;

				if (std::holds_alternative<CurveSource>(src))
					return eKineDrivenType::Deterministic;

				if (const auto* orbit = std::get_if<OrbitSource>(&src))
				{
					return (orbit->centerMode == eOrbitCenterMode::FollowTarget)
						? eKineDrivenType::TargetDerived
						: eKineDrivenType::Deterministic;
				}

				if (std::holds_alternative<FollowSource>(src))
					return eKineDrivenType::TargetDerived;

				return eKineDrivenType::RuntimeDynamic;
			}

			return eKineDrivenType::None;
		}

		bool ShouldForceNetworkPoseDriver(const PhysicsArchetypeRegistry& registry, eSpawnSource spawnSource, const PhysicsArchetypeData& data, eMotionType resolvedMotion)
		{
			if (spawnSource != eSpawnSource::Network)		return false;
			if (data.bodyType != eBodyType::Rigid)		return false;
			if (resolvedMotion != eMotionType::Kinematic)	return false;

			if (data.motionType == eMotionType::Dynamic)
				return false;

			if (data.motionType == eMotionType::Kinematic)
			{
				const eKineDrivenType kineType = ResolveKineDrivenType(registry, data);
				if (IsLocalDrivenKine(kineType))
					return false;

				return true;
			}

			return false;
		}

	} // anonymous namespace


	std::optional<RigidBody> ActorFactory::CreateRigidBody(
		PhysicsWorld&				world,
		PhysicsArchetypeKey			key,
		const PhysicsArchetypeData&	data,
		const SpawnDesc&			desc,
		ObjectId					id,
		const TargetPoseResolver&	resolver)
	{
		JAM_ASSERT(desc.IsRigid());

		PhysicsArchetypeRegistry& registry = world.Registry();

		void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(id));

		PxRigidActor* mainActor = world.CreateRigidActor(ePxSceneSlot::Main, key, ToPhysX(desc.pose), userData);
		if (!mainActor) return std::nullopt;

		PxRigidActor* replayActor = world.CreateRigidActor(ePxSceneSlot::Replay, key, ToPhysX(desc.pose), userData);
		if (!replayActor)
		{
			world.RemoveRigidActor(ePxSceneSlot::Main, mainActor);
			return std::nullopt;
		}

		const auto& overrides = std::get<RigidSpawnOverrides>(desc.overrides);
		ApplyRigidOverridesToActor(mainActor, overrides);
		ApplyRigidOverridesToActor(replayActor, overrides);

		RigidBody body{ mainActor, replayActor };
		RigidState s = body.GetMainState();

		const eMotionType resolvedMotion = ResolveMotionType(desc.spawnSrc, data);
		eKineDrivenType kineType = eKineDrivenType::None;

		if (ShouldForceNetworkPoseDriver(registry, desc.spawnSrc, data, resolvedMotion))
		{
			if (auto* dyn = mainActor->is<PxRigidDynamic>())
				dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);

			if (auto* dyn = replayActor->is<PxRigidDynamic>())
				dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);

			body.AttachBehavior(std::make_unique<KinematicRigidBehavior>(
				std::make_unique<NetworkPoseKinematicDriver>(KinematicCommon{}, NetworkPoseSource{}),
				std::make_unique<NetworkPoseKinematicDriver>(KinematicCommon{}, NetworkPoseSource{})));

			kineType = eKineDrivenType::RuntimeDynamic;
		}
		else if (auto behavior = CreateRigidBehavior(registry, data, resolver, &overrides))
		{
			body.AttachBehavior(std::move(behavior));

			if (resolvedMotion == eMotionType::Kinematic)
				kineType = ResolveKineDrivenType(registry, data);

			if (kineType == eKineDrivenType::TargetDerived)
			{
				s.kineState.targetId = desc.targetId;
			}
		}

		ApplyPackedId(*mainActor, desc.team, desc.part, desc.role);
		ApplyPackedId(*replayActor, desc.team, desc.part, desc.role);

		s.pose		= desc.pose;
		s.kineType	= kineType;

		body.ApplyStateBoth(s, true);

		return body;
	}

	std::optional<CharacterBody> ActorFactory::CreateCharacterBody(
		PhysicsWorld&				world,
		PhysicsArchetypeKey			key,
		const PhysicsArchetypeData& data,
		const SpawnDesc&			desc,
		ObjectId					id)
	{
		JAM_ASSERT(desc.IsCharacter());

		PhysicsArchetypeRegistry& registry = world.Registry();

		void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
		const auto& bodyDef = std::get<CharacterBodyData>(data.body);
		const auto& cctDef  = registry.GetCCTBodyDef(bodyDef.cct);
		const auto& moveCfg = registry.GetCharacterMoveConfig(bodyDef.moveConfig);

		PxCapsuleController* mainCCT = world.CreateController(ePxSceneSlot::Main, cctDef, ToPhysX(desc.pose.p), userData);
		if (!mainCCT) return std::nullopt;

		PxCapsuleController* replayCCT = world.CreateController(ePxSceneSlot::Replay, cctDef, ToPhysX(desc.pose.p), userData);
		if (!replayCCT)
		{
			world.RemoveController(mainCCT);
			return std::nullopt;
		}

		PxRigidActor* hitbox = nullptr;
		if (bodyDef.HasHitbox())
		{
			hitbox = world.CreateHitbox(bodyDef.hitboxes, ToPhysX(desc.pose.p), userData);
			if (!hitbox)
			{
				world.RemoveController(mainCCT);
				return std::nullopt;
			}

			ApplyPackedId(*hitbox, desc.team, desc.part, desc.role);
		}

		CharacterBody body{ mainCCT, replayCCT, hitbox, moveCfg };

		const eMotionType resolvedMotion = ResolveMotionType(desc.spawnSrc, data);

		if (resolvedMotion == eMotionType::CCT)
		{
			if (bodyDef.controllerType == eCharacterControlType::Player)
				body.SetBrain(std::make_unique<PlayerControllerComponent>());
			else if (bodyDef.controllerType == eCharacterControlType::AI)
				body.SetBrain(std::make_unique<AIControllerComponent>());
		}

		const auto& overrides = std::get<CharacterSpawnOverrides>(desc.overrides);
		CharacterState s = body.GetMainState();
		s.pos = desc.pose.p;

		if (overrides.mask.has_any(SpawnOverrideMask::VIEW_YAW))
			s.facingYaw = overrides.yaw;
		if (overrides.mask.has_any(SpawnOverrideMask::VIEW_PITCH))
			s.facingPitch = overrides.pitch;

		body.ApplyAuthorityToBoth(s);

		if (PxRigidActor* actor = mainCCT->getActor())
			ApplyPackedId(*actor, desc.team, desc.part, desc.role);
		if (replayCCT)
		{
			if (PxRigidActor* actor = replayCCT->getActor())
				ApplyPackedId(*actor, desc.team, desc.part, desc.role);
		}

		return body;
	}

	void ActorFactory::ApplyPackedId(const PxRigidActor& actor, uint16 teamId, uint8 partId, uint8 roleId)
	{
		const PxU32 n = actor.getNbShapes();
		if (n == 0) return;

		std::vector<PxShape*> shapes(n);
		actor.getShapes(shapes.data(), n);

		const auto [v] = PackedId32::Make(teamId, partId, roleId);

		for (PxShape* s : shapes)
		{
			if (!s) continue;
			JAM_ASSERT(s->isExclusive());

			PxFilterData qfd = s->getQueryFilterData();
			qfd.word2 = v;
			s->setQueryFilterData(qfd);
		}
	}

	void ActorFactory::DestroyRigidBody(PhysicsWorld& world, const RigidBody& body)
	{
		if (PxRigidActor* actor = body.GetMainActor())
			world.RemoveRigidActor(ePxSceneSlot::Main, actor);

		if (PxRigidActor* actor = body.GetReplayActor())
			world.RemoveRigidActor(ePxSceneSlot::Replay, actor);
	}

	void ActorFactory::DestroyCharacterBody(PhysicsWorld& world, const CharacterBody& body)
	{
		if (auto* hitbox = body.GetHitbox())
			world.RemoveRigidActor(ePxSceneSlot::Main, hitbox);

		if (auto* cct = body.GetMainController())
			world.RemoveController(cct);

		if (auto* cct = body.GetReplayController())
			world.RemoveController(cct);
	}


} // namespace jam::px
