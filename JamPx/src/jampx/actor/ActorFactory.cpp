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
				if (auto driver = CreateKinematicDriver(cfg))
					return std::make_unique<KinematicRigidBehavior>(std::move(driver));
			}
			else if (const auto* pHandle = bodyDef.GetBehavior<ProjectileConfigHandle>())
			{
				const ProjectileConfig& cfg = JAM_PX_PROJ_CFG(*pHandle);
				return std::make_unique<ProjectileRigidBehavior>(std::make_unique<ProjectileComponent>(cfg));
			}

			return nullptr;
		}
	}

	std::optional<RigidBody> ActorFactory::CreateRigidBody(
		PhysicsWorld&			world,
		TemplateHandle			tpl,
		const ActorTemplateDef& tplDef,
		const SpawnDesc&		desc,
		ObjectId				id)
	{
		JAM_ASSERT(desc.IsRigid())

		void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(id));

		PxRigidActor* actor = world.CreateRigidActor(tpl, ToPhysX(desc.pose), userData);
		if (!actor) return std::nullopt;

		if (auto* dyn = actor->is<PxRigidDynamic>())
		{
			const auto& overrides = std::get<RigidSpawnOverrides>(desc.overrides);
			ApplyRigidOverrides(*dyn, overrides);
		}

		RigidBody body{ actor };

		if (auto behavior = CreateRigidBehavior(tplDef))
			body.AttachBehavior(std::move(behavior));

		ApplyPackedId(*actor, desc.team, desc.part, desc.role);

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

		PxCapsuleController* controller = world.CreateController(cctDef, ToPhysX(desc.pose.p), userData);
		if (!controller) return std::nullopt;

		PxRigidActor* hitbox = nullptr;
		if (bodyDef.HasHitbox())
		{
			hitbox = world.CreateHitbox(bodyDef.hitboxes, ToPhysX(desc.pose.p), userData);
			if (!hitbox)
			{
				world.RemoveController(controller);
				return std::nullopt;
			}

			ApplyPackedId(*hitbox, desc.team, desc.part, desc.role);
		}

		CharacterBody body{ controller, hitbox, moveCfg };

		if (bodyDef.controllerType == eCharacterControlType::Player)
			body.SetBrain(std::make_unique<PlayerControllerComponent>());
		else if (bodyDef.controllerType == eCharacterControlType::AI)
			body.SetBrain(std::make_unique<AIControllerComponent>());

		const auto& overrides = std::get<CharacterSpawnOverrides>(desc.overrides);
		const float yaw   = overrides.mask.has_any(SpawnOverrideMask::VIEW_YAW) ? overrides.yaw : 0.f;
		const float pitch = overrides.mask.has_any(SpawnOverrideMask::VIEW_PITCH) ? overrides.pitch : 0.f;
		body.SetFacing(yaw, pitch);

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
			PxFilterData qfd = s->getQueryFilterData();
			qfd.word2 = v;
			s->setQueryFilterData(qfd);
		}
	}

	void ActorFactory::DestroyRigidBody(const PhysicsWorld& world, const RigidBody& body)
	{
		if (PxRigidActor* actor = body.GetActor())
			world.RemoveRigidActor(actor);
	}

	void ActorFactory::DestroyCharacterBody(PhysicsWorld& world, const CharacterBody& body)
	{
		if (PxRigidActor* hitbox = body.GetHitbox())
			world.RemoveRigidActor(hitbox);
		if (PxCapsuleController* controller = body.GetController())
			world.RemoveController(controller);
	}


} // namespace jam::px