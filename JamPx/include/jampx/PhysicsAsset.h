#pragma once
#include <variant>


#include "jampx/PhysXTypes.h"

#include "jampx/PhysicsFilter.h"

#include "jampx/kinematic/KinematicCommon.h"
#include "jampx/projectile/ProjectileComponent.h"


namespace jam::px
{

	using MaterialHandle	= Fnv1aHandle<struct MaterialDef, uint32>;
	using MeshHandle		= Fnv1aHandle<struct MeshDef, uint32>;
	using ShapeHandle		= Fnv1aHandle<struct ShapeDef, uint32>;

	using DynamicBodyHandle = Fnv1aHandle<struct DynamicBodyDef, uint32>;
	using CCTBodyHandle		= Fnv1aHandle<struct CCTBodyDef, uint32>;

	using MoveProfileHandle = Fnv1aHandle<struct MoveProfileDef, uint32>;

	using TemplateHandle	= Fnv1aHandle<struct ActorTemplateDef, uint32>;

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


	struct MaterialDef
	{
		std::string		name;
		float			staticFriction			= 0.5f;
		float			dynamicFriction			= 0.5f;
		float			restitution				= 0.1f;
	
		bool operator==(const MaterialDef&) const = default;
	};

	struct MeshDef
	{
		eMeshType		type					= eMeshType::Triangle;
		std::string		cookedPath;

		// optional
		std::string		srcPath;					
		int32			srcMeshIndex			= 0;	
		int32			srcPrimitiveIndex		= 0;	
	
		bool operator==(const MeshDef&) const = default;
	};

	struct ShapeDef
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
	};


	// meaningful only eMotionType == Dynamic
	struct DynamicBodyDef
	{
		float			density				= 1.0f;
		PxVec3			linearVelocity		= PxVec3(physx::PxZero);
		PxVec3			angularVelocity		= PxVec3(physx::PxZero);
		float			linearDamping		= 0.0f;
		float			angularDamping		= 0.05f;

		bool operator==(const DynamicBodyDef&) const = default;
	};

	enum class eCCTPolicy
	{
		Default,
	};

	struct CCTBodyDef
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

	struct RigidTemplate
	{
		std::vector<ShapeHandle>		shapes;				// at least 1

		DynamicBodyHandle				dynamic		= {};	// only meaningful if moitionType is Dynamic/Kinematic
	};

	struct CharacterTemplate
	{
		CCTBodyHandle					cct			= {};

		std::vector<ShapeHandle>		hitboxes;

		bool operator==(const CharacterTemplate&) const = default;

		bool HasHitbox() const { return !hitboxes.empty(); }
	};


	using MoveModel = std::variant<struct CharacterMoveConfig, KinematicDriverConfig, ProjectileConfig>;

	struct MoveProfileDef
	{
		std::string				name;
		MoveModel				model;
	};


	struct ActorTemplateDef
	{
		std::string			name;
		eActorType			actorType			= eActorType::Generic;
		eBodyType			representation		= eBodyType::Rigid;
		eMotionType			motionType			= eMotionType::Static;
		MotionFlag::Flags	motionFlags			= MotionFlag::NONE;
		MoveProfileHandle   moveProfile			= {};

		eSpawnPolicy		spawnPolicy			= eSpawnPolicy::Both;
		bool				allowReplication	= true;

		std::variant<RigidTemplate, CharacterTemplate> body;

		bool IsRigid()		const noexcept { return std::holds_alternative<RigidTemplate>(body); }
		bool IsCharacter()	const noexcept { return std::holds_alternative<CharacterTemplate>(body); }
	};


	struct PhysicsAsset
	{
		int32 version = 2;

		std::unordered_map<MaterialHandle, MaterialDef>			materials;
		std::unordered_map<MeshHandle, MeshDef>					meshes;
		std::unordered_map<ShapeHandle, ShapeDef>				shapes;

		std::unordered_map<DynamicBodyHandle, DynamicBodyDef>	dynBodies;
		std::unordered_map<CCTBodyHandle, CCTBodyDef>			cctBodies;

		std::unordered_map<TemplateHandle, ActorTemplateDef>	templates;
	};


	struct PhyiscsLevelOverrides
	{
		std::optional<PxVec3>        linearVelocity = std::nullopt;
		std::optional<PxVec3>        angularVelocity = std::nullopt;
		std::optional<float>         linearDamping = 0.0f;
		std::optional<float>         angularDamping = 0.0f;
	};

	struct PhysicsLevelInstanceDef
	{
		std::string					templateName;
		PxTransform					pose{ physx::PxIdentity };
		PhyiscsLevelOverrides		overrides{};
	};

	struct PhysicsLevelAsset
	{
		int32                               version = 1;
		std::vector<PhysicsLevelInstanceDef> instances;
	};




} // namespace jam::px