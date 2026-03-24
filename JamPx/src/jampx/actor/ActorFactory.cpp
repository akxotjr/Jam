#include "pch.h"
#include "jampx/actor/ActorFactory.h"
#include "jampx/actor/character/controller/AIControllerComponent.h"
#include "jampx/actor/character/controller/PlayerControllerComponent.h"
#include "jampx/actor/rigid/kinematic/IKinematicDriver.h"
#include "jampx/actor/rigid/kinematic/KinematicCommon.h"
#include "jampx/actor/rigid/kinematic/KinematicDrivers.h"
#include "jampx/actor/rigid/kinematic/KinematicRigidBehavior.h"
#include "jampx/actor/rigid/projectile/ProjectileRigidBehavior.h"

namespace jam::px
{
	namespace
	{
		void ApplyRigidOverrides(PxRigidDynamic& dyn, const RigidSpawnOverrides& overrides)
		{
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

		eMotionType ResolveMotionType(eSpawnSource spawnSource, const ActorTemplateDef& tplDef)
		{
			if (spawnSource != eSpawnSource::Network)
				return tplDef.motionType;

			// Network + Rigid(Dynamic/Kinematic) -> Kinematic(NetworkPose 추종)
			if (tplDef.bodyType == eBodyType::Rigid
				&& (tplDef.motionType == eMotionType::Dynamic || tplDef.motionType == eMotionType::Kinematic))
			{
				return eMotionType::Kinematic;
			}

			// Network + Character(CCT) -> RemoteCCT
			if (tplDef.bodyType == eBodyType::Character && tplDef.motionType == eMotionType::CCT)
			{
				return eMotionType::RemoteCCT;
			}

			return tplDef.motionType;
		}

		std::unique_ptr<IKinematicDriver> CreateKinematicDriver(const KinematicDriverConfig& cfg)
		{
			if (const auto* src = std::get_if<WaypointSource>(&cfg.source))
				return std::make_unique<WaypointKinematicDriver>(cfg.common, *src);

			if (const auto* src = std::get_if<CurveSource>(&cfg.source))
				return std::make_unique<CurveKinematicDriver>(cfg.common, *src);

			if (const auto* src = std::get_if<OrbitSource>(&cfg.source))
				return std::make_unique<OrbitKinematicDriver>(cfg.common, *src);

			if (const auto* src = std::get_if<FollowSource>(&cfg.source))
				return std::make_unique<FollowKinematicDriver>(cfg.common, *src, nullptr);

			if (const auto* src = std::get_if<NetworkPoseSource>(&cfg.source))
				return std::make_unique<NetworkPoseKinematicDriver>(cfg.common, *src);

			return nullptr;
		}

		std::unique_ptr<IRigidBehavior> CreateRigidBehavior(const ActorTemplateDef& tplDef)
		{
			if (!tplDef.IsRigid())
				return nullptr;

			const auto& bodyDef = std::get<RigidBodyDef>(tplDef.body);
			if (!bodyDef.HasBehavior())
				return nullptr;

			if (const auto* kHandle = bodyDef.GetBehavior<KinematicDriverConfigHandle>())
			{
				const KinematicDriverConfig& cfg = JAM_PX_KINE_DRIVER_CFG(*kHandle);

				auto mainDriver   = CreateKinematicDriver(cfg);
				auto replayDriver = CreateKinematicDriver(cfg);

				if (mainDriver && replayDriver)
					return std::make_unique<KinematicRigidBehavior>(std::move(mainDriver), std::move(replayDriver));
			}
			else if (const auto* pHandle = bodyDef.GetBehavior<ProjectileConfigHandle>())
			{
				const ProjectileConfig& cfg = JAM_PX_PROJ_CFG(*pHandle);

				auto mainProjectile = std::make_unique<ProjectileComponent>(cfg);
				auto replayProjectile = std::make_unique<ProjectileComponent>(cfg);

				return std::make_unique<ProjectileRigidBehavior>(std::move(mainProjectile), std::move(replayProjectile));
			}

			return nullptr;
		}

		bool ShouldForceNetworkPoseDriver(eSpawnSource spawnSource, const ActorTemplateDef& tplDef, eMotionType resolvedMotion)
		{
			if (spawnSource != eSpawnSource::Network)		return false;
			if (tplDef.bodyType != eBodyType::Rigid)		return false;
			if (resolvedMotion != eMotionType::Kinematic)	return false;

			return (tplDef.motionType == eMotionType::Dynamic || tplDef.motionType == eMotionType::Kinematic);
		}

		eKineDrivenType ResolveKineDrivenType(const ActorTemplateDef& tplDef)
		{
			if (!tplDef.IsRigid())
				return eKineDrivenType::None;

			const auto& bodyDef = std::get<RigidBodyDef>(tplDef.body);
			if (!bodyDef.HasBehavior())
				return eKineDrivenType::None;

			if (const auto* kHandle = bodyDef.GetBehavior<KinematicDriverConfigHandle>())
			{
				const KinematicDriverConfig& cfg = JAM_PX_KINE_DRIVER_CFG(*kHandle);
				const PoseSource&			 src = cfg.source;

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

				// NetworkPose or unknown -> runtime dynamic
				return eKineDrivenType::RuntimeDynamic;
			}

			return eKineDrivenType::None;
		}

	} // anonymous namespace


	std::optional<RigidBody> ActorFactory::CreateRigidBody(
		PhysicsWorld&			world,
		TemplateHandle			tpl,
		const ActorTemplateDef& tplDef,
		const SpawnDesc&		desc,
		ObjectId				id)
	{
		JAM_ASSERT(desc.IsRigid());

		void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(id));

		PxRigidActor* mainActor = world.CreateRigidActor(ePxSceneSlot::Main, tpl, ToPhysX(desc.pose), userData);
		if (!mainActor) return std::nullopt;

		PxRigidActor* replayActor = world.CreateRigidActor(ePxSceneSlot::Replay, tpl, ToPhysX(desc.pose), userData);
		if (!replayActor)
		{
			world.RemoveRigidActor(ePxSceneSlot::Main, mainActor);
			return std::nullopt;
		}

		const auto& overrides = std::get<RigidSpawnOverrides>(desc.overrides);
		ApplyRigidOverridesToActor(mainActor, overrides);
		ApplyRigidOverridesToActor(replayActor, overrides);

		RigidBody body{ mainActor, replayActor };

		const eMotionType resolvedMotion = ResolveMotionType(desc.spawnSrc, tplDef);
		eKineDrivenType kineType = eKineDrivenType::None;

		if (ShouldForceNetworkPoseDriver(desc.spawnSrc, tplDef, resolvedMotion))
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
		else if (auto behavior = CreateRigidBehavior(tplDef))
		{
			body.AttachBehavior(std::move(behavior));

			if (resolvedMotion == eMotionType::Kinematic)
				kineType = ResolveKineDrivenType(tplDef);
		}

		ApplyPackedId(*mainActor, desc.team, desc.part, desc.role);
		ApplyPackedId(*replayActor, desc.team, desc.part, desc.role);

		RigidState s = body.GetMainState();
		s.pose		= desc.pose;
		s.kineType	= kineType;
		s.kineState = {};

		body.SetMainState(s);
		body.SetReplayState(s);

		return body;
	}

	std::optional<CharacterBody> ActorFactory::CreateCharacterBody(
		PhysicsWorld&			world,
		TemplateHandle			tpl,
		const ActorTemplateDef& tplDef,
		const SpawnDesc&		desc,
		ObjectId				id)
	{
		JAM_ASSERT(desc.IsCharacter())

		void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
		const auto& bodyDef = std::get<CharacterBodyDef>(tplDef.body);
		const auto& cctDef  = JAM_PX_CCT_DEF(bodyDef.cct);
		const auto& moveCfg = JAM_PX_CHAR_MOVE_CFG(bodyDef.moveConfig);

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

		const eMotionType resolvedMotion = ResolveMotionType(desc.spawnSrc, tplDef);

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

		body.SetMainState(s);
		body.SetReplayState(s);

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