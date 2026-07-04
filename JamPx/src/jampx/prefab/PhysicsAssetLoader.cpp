#include "pch.h"

#include "jambase/JsonFileIO.h"
#include "jampx/prefab/PhysicsAssetLoader.h"

#include "jambase/Fnv1a.h"
#include <Cpp/physics_asset.generated.hpp>

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace jam::px
{
	PhysicsAssetLoader::json PhysicsAssetLoader::LoadJson(const std::string& path)
	{
		return JsonFileIO::Load(path, "failed to open physics asset file for read: ",
			[](const json&)
			{
			});
	}

	jam::shared::gen::PhysicsAssetRootDto PhysicsAssetLoader::LoadDto(const std::string& path)
	{
		return jam::shared::gen::LoadPhysicsAssetRootDto(path);
	}

	jam::shared::gen::PhysicsAssetRootDto PhysicsAssetLoader::LoadDtoFromJson(const json& json)
	{
		return jam::shared::gen::DeserializePhysicsAssetRootDto(json);
	}

	PhysicsDatabase PhysicsAssetLoader::Load(const std::string& path)
	{
		return PhysicsAssetBuilder::Build(LoadDto(path));
	}

	namespace
	{
		template <typename THandle>
		THandle MakeNamedHandle(const std::string& name)
		{
			if (name.empty())
				throw std::runtime_error("physics asset entry name must not be empty");

			return THandle::FromU64(fnv1a<uint64>(name));
		}

		void ValidateVectorSize(const std::vector<float>& values, std::size_t expectedSize, std::string_view label)
		{
			if (values.size() != expectedSize)
				throw std::runtime_error(std::string(label) + " must have exactly " + std::to_string(expectedSize) + " elements");
		}

		PxVec3 ToVec3(const std::vector<float>& values, std::string_view label)
		{
			ValidateVectorSize(values, 3, label);
			return PxVec3{ values[0], values[1], values[2] };
		}

		PxQuat ToQuat(const std::vector<float>& values, std::string_view label)
		{
			ValidateVectorSize(values, 4, label);
			PxQuat q{ values[0], values[1], values[2], values[3] };
			q.normalize();
			return q;
		}

		PxTransform ToTransform(const jam::shared::gen::TransformDto& dto)
		{
			return PxTransform{ ToVec3(dto.p, "transform.p"), ToQuat(dto.q, "transform.q") };
		}

		SimFD ToSimFD(const jam::shared::gen::SimFilterDto& dto)
		{
			PxFilterData fd{};
			fd.word0 = dto.word0;
			fd.word1 = dto.word1;
			fd.word2 = dto.word2;
			fd.word3 = dto.word3;
			return SimFD::FromPx(fd);
		}

		QueryFD ToQueryFD(const jam::shared::gen::QueryFilterDto& dto)
		{
			PxFilterData fd{};
			fd.word0 = dto.word0;
			fd.word1 = dto.word1;
			fd.word2 = dto.word2;
			fd.word3 = dto.word3;
			return QueryFD::FromPx(fd);
		}

		RequestQueryFD ToRequestQueryFD(const jam::shared::gen::QueryFilterDto& dto)
		{
			PxFilterData fd{};
			fd.word0 = dto.word0;
			fd.word1 = dto.word1;
			fd.word2 = dto.word2;
			fd.word3 = dto.word3;
			return RequestQueryFD::FromPx(fd);
		}

		eMeshType ToMeshType(jam::shared::gen::eMeshDtoType type)
		{
			switch (type)
			{
			case jam::shared::gen::eMeshDtoType::Triangle: return eMeshType::Triangle;
			case jam::shared::gen::eMeshDtoType::Convex: return eMeshType::Convex;
			}

			throw std::runtime_error("unsupported mesh dto type");
		}

		eShapeType ToShapeType(jam::shared::gen::eShapeDtoType type)
		{
			switch (type)
			{
			case jam::shared::gen::eShapeDtoType::None: return eShapeType::None;
			case jam::shared::gen::eShapeDtoType::Box: return eShapeType::Box;
			case jam::shared::gen::eShapeDtoType::Sphere: return eShapeType::Sphere;
			case jam::shared::gen::eShapeDtoType::Capsule: return eShapeType::Capsule;
			case jam::shared::gen::eShapeDtoType::Plane: return eShapeType::Plane;
			case jam::shared::gen::eShapeDtoType::TriangleMesh: return eShapeType::TriangleMesh;
			case jam::shared::gen::eShapeDtoType::ConvexMesh: return eShapeType::ConvexMesh;
			}

			throw std::runtime_error("unsupported shape dto type");
		}

		eShapeFlag ToShapeFlag(jam::shared::gen::eShapeDtoShapeFlag flag)
		{
			switch (flag)
			{
			case jam::shared::gen::eShapeDtoShapeFlag::Simulation: return eShapeFlag::Simulation;
			case jam::shared::gen::eShapeDtoShapeFlag::SimulationOnly: return eShapeFlag::SimulationOnly;
			case jam::shared::gen::eShapeDtoShapeFlag::Trigger: return eShapeFlag::Trigger;
			case jam::shared::gen::eShapeDtoShapeFlag::TriggerOnly: return eShapeFlag::TriggerOnly;
			case jam::shared::gen::eShapeDtoShapeFlag::QueryOnly: return eShapeFlag::QueryOnly;
			}

			throw std::runtime_error("unsupported shape dto flag");
		}

		eActorType ToActorType(jam::shared::gen::eRigidPhysicsArchetypeDtoActorType type)
		{
			switch (type)
			{
			case jam::shared::gen::eRigidPhysicsArchetypeDtoActorType::Generic: return eActorType::Generic;
			case jam::shared::gen::eRigidPhysicsArchetypeDtoActorType::Character: return eActorType::Character;
			case jam::shared::gen::eRigidPhysicsArchetypeDtoActorType::Projectile: return eActorType::Projectile;
			}

			return eActorType::None;
		}

		eActorType ToActorType(jam::shared::gen::eCharacterPhysicsArchetypeDtoActorType type)
		{
			switch (type)
			{
			case jam::shared::gen::eCharacterPhysicsArchetypeDtoActorType::Generic: return eActorType::Generic;
			case jam::shared::gen::eCharacterPhysicsArchetypeDtoActorType::Character: return eActorType::Character;
			case jam::shared::gen::eCharacterPhysicsArchetypeDtoActorType::Projectile: return eActorType::Projectile;
			}

			return eActorType::None;
		}

		eMotionType ToMotionType(jam::shared::gen::eRigidPhysicsArchetypeDtoMotionType type)
		{
			switch (type)
			{
			case jam::shared::gen::eRigidPhysicsArchetypeDtoMotionType::None: return eMotionType::None;
			case jam::shared::gen::eRigidPhysicsArchetypeDtoMotionType::Static: return eMotionType::Static;
			case jam::shared::gen::eRigidPhysicsArchetypeDtoMotionType::Dynamic: return eMotionType::Dynamic;
			case jam::shared::gen::eRigidPhysicsArchetypeDtoMotionType::Kinematic: return eMotionType::Kinematic;
			case jam::shared::gen::eRigidPhysicsArchetypeDtoMotionType::Cct: return eMotionType::CCT;
			case jam::shared::gen::eRigidPhysicsArchetypeDtoMotionType::RemoteCct: return eMotionType::RemoteCCT;
			}

			throw std::runtime_error("unsupported rigid motion type");
		}

		eMotionType ToMotionType(jam::shared::gen::eCharacterPhysicsArchetypeDtoMotionType type)
		{
			switch (type)
			{
			case jam::shared::gen::eCharacterPhysicsArchetypeDtoMotionType::None: return eMotionType::None;
			case jam::shared::gen::eCharacterPhysicsArchetypeDtoMotionType::Static: return eMotionType::Static;
			case jam::shared::gen::eCharacterPhysicsArchetypeDtoMotionType::Dynamic: return eMotionType::Dynamic;
			case jam::shared::gen::eCharacterPhysicsArchetypeDtoMotionType::Kinematic: return eMotionType::Kinematic;
			case jam::shared::gen::eCharacterPhysicsArchetypeDtoMotionType::Cct: return eMotionType::CCT;
			case jam::shared::gen::eCharacterPhysicsArchetypeDtoMotionType::RemoteCct: return eMotionType::RemoteCCT;
			}

			throw std::runtime_error("unsupported character motion type");
		}

		eSpawnPolicy ToSpawnPolicy(jam::shared::gen::eRigidPhysicsArchetypeDtoSpawnPolicy policy)
		{
			switch (policy)
			{
			case jam::shared::gen::eRigidPhysicsArchetypeDtoSpawnPolicy::LevelOnly: return eSpawnPolicy::LevelOnly;
			case jam::shared::gen::eRigidPhysicsArchetypeDtoSpawnPolicy::RuntimeOnly: return eSpawnPolicy::RuntimeOnly;
			case jam::shared::gen::eRigidPhysicsArchetypeDtoSpawnPolicy::Both: return eSpawnPolicy::Both;
			}

			throw std::runtime_error("unsupported rigid spawn policy");
		}

		eSpawnPolicy ToSpawnPolicy(jam::shared::gen::eCharacterPhysicsArchetypeDtoSpawnPolicy policy)
		{
			switch (policy)
			{
			case jam::shared::gen::eCharacterPhysicsArchetypeDtoSpawnPolicy::LevelOnly: return eSpawnPolicy::LevelOnly;
			case jam::shared::gen::eCharacterPhysicsArchetypeDtoSpawnPolicy::RuntimeOnly: return eSpawnPolicy::RuntimeOnly;
			case jam::shared::gen::eCharacterPhysicsArchetypeDtoSpawnPolicy::Both: return eSpawnPolicy::Both;
			}

			throw std::runtime_error("unsupported character spawn policy");
		}

		template <typename TFlags>
		MotionFlag::Flags ToMotionFlagsImpl(const TFlags& flags)
		{
			MotionFlag::Flags out = MotionFlag::NONE;
			for (const auto flag : flags)
			{
				switch (flag)
				{
				case std::remove_cvref_t<decltype(flag)>::DisableGravity: out |= MotionFlag::DISABLE_GRAVITY; break;
				case std::remove_cvref_t<decltype(flag)>::EnableCcd: out |= MotionFlag::ENABLE_CCD; break;
				case std::remove_cvref_t<decltype(flag)>::LockLinearX: out |= MotionFlag::LOCK_LINEAR_X; break;
				case std::remove_cvref_t<decltype(flag)>::LockLinearY: out |= MotionFlag::LOCK_LINEAR_Y; break;
				case std::remove_cvref_t<decltype(flag)>::LockLinearZ: out |= MotionFlag::LOCK_LINEAR_Z; break;
				case std::remove_cvref_t<decltype(flag)>::LockAngularX: out |= MotionFlag::LOCK_ANGULAR_X; break;
				case std::remove_cvref_t<decltype(flag)>::LockAngularY: out |= MotionFlag::LOCK_ANGULAR_Y; break;
				case std::remove_cvref_t<decltype(flag)>::LockAngularZ: out |= MotionFlag::LOCK_ANGULAR_Z; break;
				}
			}
			return out;
		}

		MotionFlag::Flags ToMotionFlags(const std::vector<jam::shared::gen::eRigidPhysicsArchetypeDtoMotionFlags>& flags)
		{
			return ToMotionFlagsImpl(flags);
		}

		MotionFlag::Flags ToMotionFlags(const std::vector<jam::shared::gen::eCharacterPhysicsArchetypeDtoMotionFlags>& flags)
		{
			return ToMotionFlagsImpl(flags);
		}

		eCharacterControlType ToControllerType(jam::shared::gen::eCharacterBodyDtoControllerType type)
		{
			switch (type)
			{
			case jam::shared::gen::eCharacterBodyDtoControllerType::None: return eCharacterControlType::None;
			case jam::shared::gen::eCharacterBodyDtoControllerType::Player: return eCharacterControlType::Player;
			case jam::shared::gen::eCharacterBodyDtoControllerType::Ai: return eCharacterControlType::AI;
			}

			throw std::runtime_error("unsupported controller type");
		}

		eEaseType ToEaseType(jam::shared::gen::eWaypointSourceDtoEaseType type)
		{
			return static_cast<eEaseType>(type);
		}

		eEaseType ToEaseType(jam::shared::gen::eCurveSourceDtoEaseType type)
		{
			return static_cast<eEaseType>(type);
		}

		EaseProfile ToEaseProfile(const jam::shared::gen::EaseProfileDto& dto)
		{
			EaseProfile profile{};
			profile.easeInTime = dto.easeInTime;
			profile.easeOutTime = dto.easeOutTime;
			return profile;
		}

		eWaypointLoop ToWaypointLoop(jam::shared::gen::eWaypointSourceDtoLoopMode mode)
		{
			switch (mode)
			{
			case jam::shared::gen::eWaypointSourceDtoLoopMode::Once: return eWaypointLoop::Once;
			case jam::shared::gen::eWaypointSourceDtoLoopMode::Loop: return eWaypointLoop::Loop;
			case jam::shared::gen::eWaypointSourceDtoLoopMode::PingPong: return eWaypointLoop::PingPong;
			}

			throw std::runtime_error("unsupported waypoint loop mode");
		}

		eCurveType ToCurveType(jam::shared::gen::eCurveSourceDtoType type)
		{
			switch (type)
			{
			case jam::shared::gen::eCurveSourceDtoType::CatmullRom: return eCurveType::CatmullRom;
			case jam::shared::gen::eCurveSourceDtoType::BSpline: return eCurveType::BSpline;
			case jam::shared::gen::eCurveSourceDtoType::Bezier: return eCurveType::Bezier;
			}

			throw std::runtime_error("unsupported curve type");
		}

		eOrbitCenterMode ToOrbitCenterMode(jam::shared::gen::eOrbitSourceDtoCenterMode mode)
		{
			switch (mode)
			{
			case jam::shared::gen::eOrbitSourceDtoCenterMode::FixedPoint: return eOrbitCenterMode::FixedPoint;
			case jam::shared::gen::eOrbitSourceDtoCenterMode::FollowTarget: return eOrbitCenterMode::FollowTarget;
			}

			throw std::runtime_error("unsupported orbit center mode");
		}

		eOrbitPlaneMode ToOrbitPlaneMode(jam::shared::gen::eOrbitSourceDtoPlaneMode mode)
		{
			switch (mode)
			{
			case jam::shared::gen::eOrbitSourceDtoPlaneMode::Xy: return eOrbitPlaneMode::XY;
			case jam::shared::gen::eOrbitSourceDtoPlaneMode::Xz: return eOrbitPlaneMode::XZ;
			case jam::shared::gen::eOrbitSourceDtoPlaneMode::Yz: return eOrbitPlaneMode::YZ;
			case jam::shared::gen::eOrbitSourceDtoPlaneMode::Custom: return eOrbitPlaneMode::Custom;
			}

			throw std::runtime_error("unsupported orbit plane mode");
		}

		eOrbitRadiusMode ToOrbitRadiusMode(jam::shared::gen::eOrbitSourceDtoRadiusMode mode)
		{
			switch (mode)
			{
			case jam::shared::gen::eOrbitSourceDtoRadiusMode::Circle: return eOrbitRadiusMode::Circle;
			case jam::shared::gen::eOrbitSourceDtoRadiusMode::Ellipse: return eOrbitRadiusMode::Ellipse;
			}

			throw std::runtime_error("unsupported orbit radius mode");
		}

		eOrbitEndMode ToOrbitEndMode(jam::shared::gen::eOrbitSourceDtoEndMode mode)
		{
			switch (mode)
			{
			case jam::shared::gen::eOrbitSourceDtoEndMode::Loop: return eOrbitEndMode::Loop;
			case jam::shared::gen::eOrbitSourceDtoEndMode::PingPong: return eOrbitEndMode::PingPong;
			case jam::shared::gen::eOrbitSourceDtoEndMode::Clamp: return eOrbitEndMode::Clamp;
			}

			throw std::runtime_error("unsupported orbit end mode");
		}

		eOrbitOrientationMode ToOrbitOrientationMode(jam::shared::gen::eOrbitSourceDtoOrientationMode mode)
		{
			switch (mode)
			{
			case jam::shared::gen::eOrbitSourceDtoOrientationMode::KeepRotation: return eOrbitOrientationMode::KeepRotation;
			case jam::shared::gen::eOrbitSourceDtoOrientationMode::FaceCenter: return eOrbitOrientationMode::FaceCenter;
			case jam::shared::gen::eOrbitSourceDtoOrientationMode::OrientAlongVelocity: return eOrbitOrientationMode::OrientAlongVelocity;
			}

			throw std::runtime_error("unsupported orbit orientation mode");
		}

		eFollowOffsetSpace ToFollowOffsetSpace(jam::shared::gen::eFollowSourceDtoOffsetSpace space)
		{
			switch (space)
			{
			case jam::shared::gen::eFollowSourceDtoOffsetSpace::TargetLocal: return eFollowOffsetSpace::TargetLocal;
			case jam::shared::gen::eFollowSourceDtoOffsetSpace::World: return eFollowOffsetSpace::World;
			}

			throw std::runtime_error("unsupported follow offset space");
		}

		eFollowRotationMode ToFollowRotationMode(jam::shared::gen::eFollowSourceDtoRotationMode mode)
		{
			switch (mode)
			{
			case jam::shared::gen::eFollowSourceDtoRotationMode::KeepWorldRotation: return eFollowRotationMode::KeepWorldRotation;
			case jam::shared::gen::eFollowSourceDtoRotationMode::MatchTargetRotation: return eFollowRotationMode::MatchTargetRotation;
			case jam::shared::gen::eFollowSourceDtoRotationMode::LookAtTarget: return eFollowRotationMode::LookAtTarget;
			case jam::shared::gen::eFollowSourceDtoRotationMode::OrientAlongVelocity: return eFollowRotationMode::OrientAlongVelocity;
			}

			throw std::runtime_error("unsupported follow rotation mode");
		}

		eProjectileKind ToProjectileKind(jam::shared::gen::eProjectileConfigDtoKind kind)
		{
			switch (kind)
			{
			case jam::shared::gen::eProjectileConfigDtoKind::DynSim: return eProjectileKind::DYN_SIM;
			case jam::shared::gen::eProjectileConfigDtoKind::Analytic: return eProjectileKind::ANALYTIC;
			case jam::shared::gen::eProjectileConfigDtoKind::Hitscan: return eProjectileKind::HITSCAN;
			}

			throw std::runtime_error("unsupported projectile kind");
		}

		eProjectileMotionModel ToProjectileMotionModel(jam::shared::gen::eProjectileMotionConfigDtoModel model)
		{
			switch (model)
			{
			case jam::shared::gen::eProjectileMotionConfigDtoModel::Linear: return eProjectileMotionModel::Linear;
			case jam::shared::gen::eProjectileMotionConfigDtoModel::Ballistic: return eProjectileMotionModel::Ballistic;
			case jam::shared::gen::eProjectileMotionConfigDtoModel::HomingSteer: return eProjectileMotionModel::HomingSteer;
			case jam::shared::gen::eProjectileMotionConfigDtoModel::HomingLead: return eProjectileMotionModel::HomingLead;
			case jam::shared::gen::eProjectileMotionConfigDtoModel::HomingPn: return eProjectileMotionModel::HomingPN;
			}

			throw std::runtime_error("unsupported projectile motion model");
		}

		eProjectileHitModel ToProjectileHitModel(jam::shared::gen::eProjectileHitConfigDtoModel model)
		{
			switch (model)
			{
			case jam::shared::gen::eProjectileHitConfigDtoModel::RaycastFallback: return eProjectileHitModel::RaycastFallback;
			case jam::shared::gen::eProjectileHitConfigDtoModel::ShapeSweep: return eProjectileHitModel::ShapeSweep;
			case jam::shared::gen::eProjectileHitConfigDtoModel::SphereSweep: return eProjectileHitModel::SphereSweep;
			case jam::shared::gen::eProjectileHitConfigDtoModel::ExpandingShapeSweep: return eProjectileHitModel::ExpandingShapeSweep;
			case jam::shared::gen::eProjectileHitConfigDtoModel::ExpandingSphereSweep: return eProjectileHitModel::ExpandingSphereSweep;
			}

			throw std::runtime_error("unsupported projectile hit model");
		}

		template <typename TMap, typename TKey, typename TValue>
		void EmplaceChecked(TMap& map, TKey key, TValue&& value, const std::string& name, std::string_view label)
		{
			if (!map.emplace(key, std::forward<TValue>(value)).second)
				throw std::runtime_error("duplicate " + std::string(label) + ": " + name);
		}
	}

	PhysicsDatabase PhysicsAssetBuilder::Build(const jam::shared::gen::PhysicsAssetRootDto& dto)
	{
		PhysicsDatabase database{};
		database.version = dto.version;
		if (database.version != 1)
			throw std::runtime_error("unsupported physics asset version");

		for (const auto& [name, materialDto] : dto.materials)
		{
			MaterialData data{};
			data.name = name;
			data.staticFriction = materialDto.staticFriction;
			data.dynamicFriction = materialDto.dynamicFriction;
			data.restitution = materialDto.restitution;
			EmplaceChecked(database.materials, MakeNamedHandle<MaterialHandle>(name), std::move(data), name, "material");
		}

		for (const auto& [name, meshDto] : dto.meshes)
		{
			MeshData data{};
			data.type = ToMeshType(meshDto.type);
			data.cookedPath = meshDto.cookedPath;
			data.srcPath = meshDto.srcPath;
			data.srcMeshIndex = meshDto.srcMeshIndex;
			data.srcPrimitiveIndex = meshDto.srcPrimitiveIndex;
			EmplaceChecked(database.meshes, MakeNamedHandle<MeshHandle>(name), std::move(data), name, "mesh");
		}

		for (const auto& [name, shapeDto] : dto.shapes)
		{
			if (shapeDto.material.empty())
				throw std::runtime_error("shape requires material ref: " + name);

			ShapeData data{};
			data.type = ToShapeType(shapeDto.type);
			data.localPose = ToTransform(shapeDto.localPose);
			data.material = MakeNamedHandle<MaterialHandle>(shapeDto.material);
			data.shapeFlag = ToShapeFlag(shapeDto.shapeFlag);
			data.simFD = ToSimFD(shapeDto.simFilter);
			data.qryFD = ToQueryFD(shapeDto.qryFilter);
			data.contactOffset = shapeDto.contactOffset;
			data.restOffset = shapeDto.restOffset;
			if (!shapeDto.halfExtents.empty())
				data.halfExtents = ToVec3(shapeDto.halfExtents, "shape.halfExtents");
			data.radius = shapeDto.radius;
			data.halfHeight = shapeDto.halfHeight;
			if (!shapeDto.mesh.empty())
				data.mesh = MakeNamedHandle<MeshHandle>(shapeDto.mesh);
			EmplaceChecked(database.shapes, MakeNamedHandle<ShapeHandle>(name), std::move(data), name, "shape");
		}

		for (const auto& [name, dynamicDto] : dto.dynBodies)
		{
			DynamicBodyData data{};
			data.density = dynamicDto.density;
			if (!dynamicDto.linearVelocity.empty())
				data.linearVelocity = ToVec3(dynamicDto.linearVelocity, "dynamic.linearVelocity");
			if (!dynamicDto.angularVelocity.empty())
				data.angularVelocity = ToVec3(dynamicDto.angularVelocity, "dynamic.angularVelocity");
			data.linearDamping = dynamicDto.linearDamping;
			data.angularDamping = dynamicDto.angularDamping;
			EmplaceChecked(database.dynBodies, MakeNamedHandle<DynamicBodyHandle>(name), std::move(data), name, "dynamic body");
		}

		for (const auto& [name, cctDto] : dto.cctBodies)
		{
			if (cctDto.material.empty())
				throw std::runtime_error("cct body requires material ref: " + name);

			CCTBodyData data{};
			data.radius = cctDto.radius;
			data.height = cctDto.height;
			data.material = MakeNamedHandle<MaterialHandle>(cctDto.material);
			data.density = cctDto.density;
			data.policy = eCCTPolicy::Default;
			data.slopeLimit = cctDto.slopeLimit;
			data.invisibleWallHeight = cctDto.invisibleWallHeight;
			data.maxJumpHeight = cctDto.maxJumpHeight;
			data.contactOffset = cctDto.contactOffset;
			data.stepOffset = cctDto.stepOffset;
			data.scaleCoeff = cctDto.scaleCoeff;
			data.volumeGrowth = cctDto.volumeGrowth;
			EmplaceChecked(database.cctBodies, MakeNamedHandle<CCTBodyHandle>(name), std::move(data), name, "cct body");
		}

		for (const auto& [name, moveDto] : dto.charMoveConfigs)
		{
			CharacterMoveConfig data{};
			data.gravity = moveDto.gravity;
			data.groundAccel = moveDto.groundAccel;
			data.groundFriction = moveDto.groundFriction;
			data.groundMaxSpeed = moveDto.groundMaxSpeed;
			data.airAccel = moveDto.airAccel;
			data.airMaxSpeed = moveDto.airMaxSpeed;
			data.capHorizontalOnly = moveDto.capHorizontalOnly;
			data.hardSpeedCapAir = moveDto.hardSpeedCapAir;
			data.softCapStartAir = moveDto.softCapStartAir;
			data.softCapStrengthAir = moveDto.softCapStrengthAir;
			data.stance.standingHeight = moveDto.stance.standingHeight;
			data.stance.crouchHeight = moveDto.stance.crouchHeight;
			data.stance.crouchSpeedMultiplier = moveDto.stance.crouchSpeedMultiplier;
			data.stance.holdToCrouch = moveDto.stance.holdToCrouch;
			data.stance.proneHeight = moveDto.stance.proneHeight;
			data.stance.proneSpeedMultiplier = moveDto.stance.proneSpeedMultiplier;
			data.stance.holdToProne = moveDto.stance.holdToProne;
			data.gait.walkSpeedMultiplier = moveDto.gait.walkSpeedMultiplier;
			data.gait.runSpeedMultiplier = moveDto.gait.runSpeedMultiplier;
			data.gait.sprintSpeedMultiplier = moveDto.gait.sprintSpeedMultiplier;
			data.gait.sprintAccelMultiplier = moveDto.gait.sprintAccelMultiplier;
			data.gait.sprintMinSpeedToStart = moveDto.gait.sprintMinSpeedToStart;
			data.gait.sprintAllowInAir = moveDto.gait.sprintAllowInAir;
			data.jump.speed = moveDto.jump.speed;
			data.jump.coyoteTime = moveDto.jump.coyoteTime;
			data.jump.jumpBuffer = moveDto.jump.jumpBuffer;
			data.jump.edgeTrigger = moveDto.jump.edgeTrigger;
			data.dash.speed = moveDto.dash.speed;
			data.dash.duration = moveDto.dash.duration;
			data.dash.overrideLocomotion = moveDto.dash.overrideLocomotion;
			data.dash.allowInAir = moveDto.dash.allowInAir;
			data.dash.endOnCollision = moveDto.dash.endOnCollision;
			data.dash.steerFactor = moveDto.dash.steerFactor;
			EmplaceChecked(database.charMoveConfigs, MakeNamedHandle<CharacterMoveConfigHandle>(name), std::move(data), name, "character move config");
		}

		for (const auto& [name, kinematicDto] : dto.kinematicDriverConfigs)
		{
			if (!kinematicDto.source)
				throw std::runtime_error("kinematic driver config requires source: " + name);

			KinematicDriverConfig data{};
			data.common.computeDerivedVel = kinematicDto.common.computeDerivedVel;
			data.common.carryRiders = kinematicDto.common.carryRiders;
			data.common.sweep = kinematicDto.common.sweep;
			data.common.maxSpeed = kinematicDto.common.maxSpeed;

			if (const auto* waypoint = dynamic_cast<const jam::shared::gen::WaypointSourceDto*>(kinematicDto.source.get()))
			{
				WaypointSource source{};
				for (const auto& waypointDto : waypoint->waypoints)
				{
					KinematicWaypoint runtimeWaypoint{};
					runtimeWaypoint.pose = ToTransform(waypointDto.pose);
					runtimeWaypoint.pauseDuration = waypointDto.pauseDuration;
					source.waypoints.push_back(runtimeWaypoint);
				}
				source.speed = waypoint->speed;
				source.loopMode = ToWaypointLoop(waypoint->loopMode);
				source.useEaseProfile = waypoint->useEaseProfile;
				source.easeType = ToEaseType(waypoint->easeType);
				source.easeProfile = ToEaseProfile(waypoint->easeProfile);
				data.source = std::move(source);
			}
			else if (const auto* curve = dynamic_cast<const jam::shared::gen::CurveSourceDto*>(kinematicDto.source.get()))
			{
				CurveSource source{};
				for (const auto& controlPoint : curve->controlPoints)
					source.controlPoints.push_back(ToVec3(controlPoint, "curve.controlPoints"));
				source.type = ToCurveType(curve->type);
				source.speed = curve->speed;
				source.duration = curve->duration;
				source.loop = curve->loop;
				source.buildSegments = curve->buildSegments;
				source.useEaseProfile = curve->useEaseProfile;
				source.easeType = ToEaseType(curve->easeType);
				source.easeProfile = ToEaseProfile(curve->easeProfile);
				source.alpha = curve->alpha;
				source.degree = curve->degree;
				data.source = std::move(source);
			}
			else if (const auto* orbit = dynamic_cast<const jam::shared::gen::OrbitSourceDto*>(kinematicDto.source.get()))
			{
				OrbitSource source{};
				source.centerMode = ToOrbitCenterMode(orbit->centerMode);
				if (!orbit->fixedCenter.empty())
					source.fixedCenter = ToVec3(orbit->fixedCenter, "orbit.fixedCenter");
				if (!orbit->targetOffset.empty())
					source.targetOffset = ToVec3(orbit->targetOffset, "orbit.targetOffset");
				source.planeMode = ToOrbitPlaneMode(orbit->planeMode);
				if (!orbit->customPlaneNormal.empty())
					source.customPlaneNormal = ToVec3(orbit->customPlaneNormal, "orbit.customPlaneNormal");
				source.radiusMode = ToOrbitRadiusMode(orbit->radiusMode);
				source.radius = orbit->radius;
				if (!orbit->ellipseRadius.empty())
				{
					ValidateVectorSize(orbit->ellipseRadius, 2, "orbit.ellipseRadius");
					source.ellipseRadius = PxVec2(orbit->ellipseRadius[0], orbit->ellipseRadius[1]);
				}
				source.initialAngleRad = orbit->initialAngleRad;
				source.angularSpeedRad = orbit->angularSpeedRad;
				source.endMode = ToOrbitEndMode(orbit->endMode);
				source.minAngleRad = orbit->minAngleRad;
				source.maxAngleRad = orbit->maxAngleRad;
				source.orientationMode = ToOrbitOrientationMode(orbit->orientationMode);
				if (!orbit->initialRotation.empty())
					source.initialRotation = ToQuat(orbit->initialRotation, "orbit.initialRotation");
				source.useEaseAtEnds = orbit->useEaseAtEnds;
				source.endEaseProfile = ToEaseProfile(orbit->endEaseProfile);
				source.computeDerivedVelocity = orbit->computeDerivedVelocity;
				data.source = std::move(source);
			}
			else if (const auto* follow = dynamic_cast<const jam::shared::gen::FollowSourceDto*>(kinematicDto.source.get()))
			{
				FollowSource source{};
				source.targetId = follow->targetId;
				if (!follow->offset.empty())
					source.offset = ToVec3(follow->offset, "follow.offset");
				source.offsetSpace = ToFollowOffsetSpace(follow->offsetSpace);
				source.positionFollowSpeed = follow->positionFollowSpeed;
				source.rotationFollowSpeed = follow->rotationFollowSpeed;
				source.maxLinearSpeed = follow->maxLinearSpeed;
				source.maxAngularSpeed = follow->maxAngularSpeed;
				source.rotationMode = ToFollowRotationMode(follow->rotationMode);
				source.snapIfTargetMissing = follow->snapIfTargetMissing;
				source.keepLastPoseIfMissing = follow->keepLastPoseIfMissing;
				source.computeDerivedVelocity = follow->computeDerivedVelocity;
				data.source = std::move(source);
			}
			else if (const auto* networkPose = dynamic_cast<const jam::shared::gen::NetworkPoseSourceDto*>(kinematicDto.source.get()))
			{
				NetworkPoseSource source{};
				source.computeDerivedVelocity = networkPose->computeDerivedVelocity;
				data.source = source;
			}
			else
			{
				throw std::runtime_error("unsupported kinematic source dto type: " + name);
			}

			EmplaceChecked(database.kinematicDriverConfigs, MakeNamedHandle<KinematicDriverConfigHandle>(name), std::move(data), name, "kinematic driver config");
		}

		for (const auto& [name, projectileDto] : dto.projectileConfigs)
		{
			ProjectileConfig data{};
			data.kind = ToProjectileKind(projectileDto.kind);
			data.motion.model = ToProjectileMotionModel(projectileDto.motion.model);
			if (!projectileDto.motion.initialVelocity.empty())
				data.motion.initialVelocity = ToVec3(projectileDto.motion.initialVelocity, "projectile.motion.initialVelocity");
			data.motion.gravityScale = projectileDto.motion.gravityScale;
			data.hit.model = ToProjectileHitModel(projectileDto.hit.model);
			data.hit.useShapeSweep = projectileDto.hit.useShapeSweep;
			data.hit.fallbackRaycast = projectileDto.hit.fallbackRaycast;
			data.hit.requestFd = ToRequestQueryFD(projectileDto.hit.requestFd);
			data.lifetime.maxRange = projectileDto.lifetime.maxRange;
			data.lifetime.maxLifetime = projectileDto.lifetime.maxLifetime;
			data.homing.targetId = projectileDto.homing.targetId;
			data.homing.maxSpeed = projectileDto.homing.maxSpeed;
			data.homing.acceleration = projectileDto.homing.acceleration;
			data.homing.maxTurnRate = projectileDto.homing.maxTurnRate;
			data.homing.enableHoming = projectileDto.homing.enableHoming;
			data.homing.keepSpeedConstant = projectileDto.homing.keepSpeedConstant;
			data.homing.reacquireTarget = projectileDto.homing.reacquireTarget;
			data.homing.keepLastDirection = projectileDto.homing.keepLastDirection;
			data.homing.leadTimeScale = projectileDto.homing.leadTimeScale;
			data.homing.maxPredictTime = projectileDto.homing.maxPredictTime;
			data.homing.navigationGain = projectileDto.homing.navigationGain;
			data.homing.maxLateralAccel = projectileDto.homing.maxLateralAccel;
			EmplaceChecked(database.projectileConfigs, MakeNamedHandle<ProjectileConfigHandle>(name), std::move(data), name, "projectile config");
		}

		for (const auto& [name, archetypeDtoPtr] : dto.archetypes)
		{
			if (!archetypeDtoPtr)
				throw std::runtime_error("physics archetype dto must not be null: " + name);

			PhysicsArchetypeData data{};
			data.name = name;

			if (const auto* rigid = dynamic_cast<const jam::shared::gen::RigidPhysicsArchetypeDto*>(archetypeDtoPtr.get()))
			{
				data.actorType = ToActorType(rigid->actorType);
				data.bodyType = eBodyType::Rigid;
				data.motionType = ToMotionType(rigid->motionType);
				data.motionFlags = ToMotionFlags(rigid->motionFlags);
				data.spawnPolicy = ToSpawnPolicy(rigid->spawnPolicy);
				data.allowReplication = rigid->allowReplication;

				RigidBodyData body{};
				for (const auto& shapeName : rigid->body.shapes)
					body.shapes.push_back(MakeNamedHandle<ShapeHandle>(shapeName));
				if (!rigid->body.dynamic.empty())
					body.dynamic = MakeNamedHandle<DynamicBodyHandle>(rigid->body.dynamic);
				if (!rigid->body.behavior.config.empty())
				{
					switch (rigid->body.behavior.kind)
					{
					case jam::shared::gen::eRigidBehaviorDtoKind::None:
						break;
					case jam::shared::gen::eRigidBehaviorDtoKind::KinematicDriver:
						body.behavior = MakeNamedHandle<KinematicDriverConfigHandle>(rigid->body.behavior.config);
						break;
					case jam::shared::gen::eRigidBehaviorDtoKind::Projectile:
						body.behavior = MakeNamedHandle<ProjectileConfigHandle>(rigid->body.behavior.config);
						break;
					}
				}
				data.body = std::move(body);
			}
			else if (const auto* character = dynamic_cast<const jam::shared::gen::CharacterPhysicsArchetypeDto*>(archetypeDtoPtr.get()))
			{
				data.actorType = ToActorType(character->actorType);
				data.bodyType = eBodyType::Character;
				data.motionType = ToMotionType(character->motionType);
				data.motionFlags = ToMotionFlags(character->motionFlags);
				data.spawnPolicy = ToSpawnPolicy(character->spawnPolicy);
				data.allowReplication = character->allowReplication;

				if (character->body.cct.empty())
					throw std::runtime_error("character archetype requires cct: " + name);

				CharacterBodyData body{};
				body.cct = MakeNamedHandle<CCTBodyHandle>(character->body.cct);
				for (const auto& hitboxName : character->body.hitboxes)
					body.hitboxes.push_back(MakeNamedHandle<ShapeHandle>(hitboxName));
				body.controllerType = ToControllerType(character->body.controllerType);
				if (!character->body.moveConfig.empty())
					body.moveConfig = MakeNamedHandle<CharacterMoveConfigHandle>(character->body.moveConfig);
				data.body = std::move(body);
			}
			else
			{
				throw std::runtime_error("unsupported physics archetype dto type: " + name);
			}

			EmplaceChecked(database.archetypes, MakePhysicsArchetypeKey(name), std::move(data), name, "physics archetype");
		}

		return database;
	}
}
