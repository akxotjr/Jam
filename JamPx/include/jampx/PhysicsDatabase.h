#pragma once
#include <variant>

#include <jambase/JamAsset.h>

#include "jampx/PhysXTypes.h"
#include "jampx/PhysicsFilter.h"
#include "jampx/actor/character/controller/ICharacterController.h"
#include "actor/character/CharacterMovementTypes.h"
#include "actor/rigid/kinematic/KinematicCommon.h"
#include "actor/rigid/projectile/ProjectileComponent.h"

namespace jam::px
{
	enum class eCharacterControlType : uint8;

	struct MaterialTag;
	struct MeshTag;
	struct ShapeTag;
	struct DynamicBodyTag;
	struct CCTBodyTag;
	struct CharacterMoveConfigTag;
	struct KinematicDriverConfigTag;
	struct ProjectileConfigTag;
	struct PhysicsAssetTag;

	using MaterialHandle				= AssetKey<MaterialTag>;
	using MeshHandle					= AssetKey<MeshTag>;
	using ShapeHandle					= AssetKey<ShapeTag>;
	using DynamicBodyHandle				= AssetKey<DynamicBodyTag>;
	using CCTBodyHandle					= AssetKey<CCTBodyTag>;
	using CharacterMoveConfigHandle		= AssetKey<CharacterMoveConfigTag>;
	using KinematicDriverConfigHandle	= AssetKey<KinematicDriverConfigTag>;
	using ProjectileConfigHandle		= AssetKey<ProjectileConfigTag>;
	using RigidBehaviorHandle			= std::variant<std::monostate, KinematicDriverConfigHandle, ProjectileConfigHandle>;
	using PhysicsAssetKey				= AssetKey<PhysicsAssetTag>;

	enum class eShapeType
	{
		None,
		Box,
		Sphere,
		Capsule,
		Plane,

		TriangleMesh,
		ConvexMesh,
	};

	static bool IsPrimitiveShape(eShapeType type)
	{
		return type == eShapeType::Box | type == eShapeType::Sphere | type == eShapeType::Sphere | type == eShapeType::Capsule | type == eShapeType::Plane;
	}

	enum class eMeshType
	{
		Triangle,
		Convex
	};

	enum class eSpawnPolicy : uint8
	{
		LevelOnly,
		RuntimeOnly,
		Both
	};


	struct MaterialData
	{
		std::string		name;
		float			staticFriction			= 0.5f;
		float			dynamicFriction			= 0.5f;
		float			restitution				= 0.1f;
	
		bool operator==(const MaterialData&) const = default;
	};

	struct MeshData
	{
		eMeshType		type					= eMeshType::Triangle;
		std::string		cookedPath;

		// optional
		std::string		srcPath;					
		int32			srcMeshIndex			= 0;	
		int32			srcPrimitiveIndex		= 0;	
	
		bool operator==(const MeshData&) const = default;
	};

	struct ShapeData
	{
		eShapeType		type				= eShapeType::None;
		PxTransform		localPose			= PxTransform(physx::PxIdentity);
		MaterialHandle	material			= {};
		eShapeFlag		shapeFlag			= eShapeFlag::Simulation;
	
		SimFD			simFD				= {};
		QueryFD			qryFD				= {};

		// advance
		float			contactOffset		= 0.0f;
		float			restOffset			= 0.02f;
	
		// primitive geometry params
		PxVec3			halfExtents			= { 0.5f, 0.5f, 0.5f };		// box
		float			radius				= 0.5f;								// sphere/capsule
		float			halfHeight			= 0.5f;								// capsule

		// mesh geometry params
		MeshHandle		mesh				= {};

		bool IsPrimitiveGeometry() const {
			return (type == eShapeType::Box)
				|| (type == eShapeType::Capsule)
				|| (type == eShapeType::Sphere)
				|| (type == eShapeType::Plane);
		}

		bool IsMeshGeometry() const
		{
			return (type == eShapeType::TriangleMesh) || (type == eShapeType::ConvexMesh);
		}
	};


	// meaningful only eMotionType == Dynamic
	struct DynamicBodyData
	{
		float			density				= 1.0f;
		PxVec3			linearVelocity		= PxVec3(physx::PxZero);
		PxVec3			angularVelocity		= PxVec3(physx::PxZero);
		float			linearDamping		= 0.0f;
		float			angularDamping		= 0.05f;

		bool operator==(const DynamicBodyData&) const = default;
	};

	enum class eCCTPolicy
	{
		Default,
	};

	struct CCTBodyData
	{
		float			radius				= 0.5f;
		float			height				= 0.75f;
		MaterialHandle	material			= {};
		float			density				= 10.0f;
		eCCTPolicy		policy				= eCCTPolicy::Default;

		// advance
		float			slopeLimit			= 0.707f;
		float			invisibleWallHeight = 0.0f;
		float			maxJumpHeight		= 0.0f;
		float			contactOffset		= 0.1f;
		float			stepOffset			= 0.5f;
		float			scaleCoeff			= 0.8f;
		float			volumeGrowth		= 1.5f;
	};


	struct RigidBodyData
	{
		std::vector<ShapeHandle>		shapes;				// at least 1
		DynamicBodyHandle				dynamic	 = {};		// only meaningful if moitionType is Dynamic/Kinematic
		RigidBehaviorHandle				behavior = {};		// only meaningful if moitionType is Kinematic or actorType is Projectile

		bool HasBehavior() const { return !std::holds_alternative<std::monostate>(behavior); }

		template<class T>
		const T* GetBehavior() const { return std::get_if<T>(&behavior); }
	};

	struct CharacterBodyData
	{
		CCTBodyHandle					cct				= {};
		std::vector<ShapeHandle>		hitboxes;
		eCharacterControlType			controllerType	= eCharacterControlType::Player;
		CharacterMoveConfigHandle		moveConfig		= {};

		bool operator==(const CharacterBodyData&) const = default;
		bool HasHitbox() const { return !hitboxes.empty(); }
	};


	struct PhysicsArchetypeData
	{
		std::string			name;
		eActorType			actorType			= eActorType::Generic;
		eBodyType			bodyType			= eBodyType::Rigid;
		eMotionType			motionType			= eMotionType::Static;
		MotionFlag::Flags	motionFlags			= MotionFlag::NONE;

		eSpawnPolicy		spawnPolicy			= eSpawnPolicy::Both;
		bool				allowReplication	= true;

		std::variant<RigidBodyData, CharacterBodyData> body;

		bool IsRigid()		const noexcept { return std::holds_alternative<RigidBodyData>(body); }
		bool IsCharacter()	const noexcept { return std::holds_alternative<CharacterBodyData>(body); }
	};


	struct PhysicsDatabase
	{
		int32 version = 1;

		std::unordered_map<MaterialHandle, MaterialData>							materials;
		std::unordered_map<MeshHandle, MeshData>									meshes;
		std::unordered_map<ShapeHandle, ShapeData>									shapes;

		std::unordered_map<DynamicBodyHandle, DynamicBodyData>						dynBodies;
		std::unordered_map<CCTBodyHandle, CCTBodyData>								cctBodies;
		std::unordered_map<CharacterMoveConfigHandle, CharacterMoveConfig>			charMoveConfigs;
		std::unordered_map<KinematicDriverConfigHandle, KinematicDriverConfig>		kinematicDriverConfigs;
		std::unordered_map<ProjectileConfigHandle, ProjectileConfig>				projectileConfigs;

		std::unordered_map<PhysicsArchetypeKey, PhysicsArchetypeData>				archetypes;
	};

} // namespace jam::px
