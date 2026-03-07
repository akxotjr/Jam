#pragma once
#include <variant>

#include "api/PhysicsTypes.h"
#include "kinematic/KinematicCommon.h"
#include "prefab/PrefabAssets.h"


namespace jam::px
{

	using MaterialHandle	= jam::Fnv1aHandle<struct MaterialDef, uint32>;
	using MeshHandle		= jam::Fnv1aHandle<struct MeshDef, uint32>;
	using ShapeHandle		= jam::Fnv1aHandle<struct ShapeDef, uint32>;

	using DynamicBodyHandle = jam::Fnv1aHandle<struct DynamicBodyDef, uint32>;
	using CCTBodyHandle		= jam::Fnv1aHandle<struct CCTBodyDef, uint32>;

	using MoveProfileHandle = jam::Fnv1aHandle<struct MoveProfileDef, uint32>;

	using TemplateHandle	= jam::Fnv1aHandle<struct ActorTemplateDef, uint32>;

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


	using MoveModel = std::variant<CharacterMoveConfig, KinematicMoveConfig, ProjectileMoveConfig>;

	struct MoveProfileDef
	{
		std::string				name;
		MoveModel				model;
	};


	struct ActorTemplateDef
	{
		std::string			name;
		eActorType			actorType			= eActorType::Generic;
		ePhyiscsRep			representation		= ePhyiscsRep::Rigid;
		eMotionType			motionType			= eMotionType::Static;
		BodyFlag::Flags		bodyFlags			= BodyFlag::NONE;
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
		std::string             templateName;
		PxTransform             pose{physx::PxIdentity };
		PhyiscsLevelOverrides    overrides{};
	};

	struct PhysicsLevelAsset
	{
		int32                               version = 1;
		std::vector<PhysicsLevelInstanceDef> instances;
	};


	enum class eSpawnSource : uint8
	{
		Level		= 0,
		Runtime		= 1,
		Network		= 2,
		Tool		= 3,
	};

	struct SpawnOverrideMask
	{
		enum Enum
		{
			NONE			= 0,
			LINEAR_VEL		= 1 << 0,
			ANGULAR_VEL		= 1 << 1,
			LINEAR_DAMP		= 1 << 2,
			ANGULAR_DAMP	= 1 << 3,

			VIEW			= 1 << 8,
			CHAR_VEL		= 1 << 9,
			
		};

		using Flag = FlagsT<Enum>;
	};


	struct RigidSpawnOverrides
	{
		SpawnOverrideMask::Flag mask = SpawnOverrideMask::NONE;

		PxVec3	linearVelocity		= PxVec3(physx::PxZero);
		PxVec3	angularVelocity		= PxVec3(physx::PxZero);
		float	linearDamping		= 0.0f;
		float	angularDamping		= 0.0f;
	};

	struct CharacterSpawnOverrides
	{
		SpawnOverrideMask::Flag mask = SpawnOverrideMask::NONE;

		float	yaw					= 0.0f;
		float	pitch				= 0.0f;
	};


	struct SpawnDesc
	{
		TemplateHandle		tpl{};
		PxTransform			pose		= PxTransform(physx::PxIdentity);
		eSpawnSource		spawnSrc	= eSpawnSource::Level;

		std::variant<RigidSpawnOverrides, CharacterSpawnOverrides> overrides;

		bool IsRigid() const noexcept { return std::holds_alternative<RigidSpawnOverrides>(overrides); }
		bool IsCharacter() const noexcept { return std::holds_alternative<CharacterSpawnOverrides>(overrides); }
	};




	struct PhysicsState
	{
		TemplateHandle tpl{};
		std::variant<RigidState, CharacterState> state;

		bool IsRigid() const noexcept { return std::holds_alternative<RigidState>(state); }
		bool IsCharacter() const noexcept { return std::holds_alternative<CharacterState>(state); }
	};

	struct PhysicsRuntimeRefs
	{
		PxRigidActor* actor = nullptr; // rigid
		PxController* cct	= nullptr; // character
	};


} // namespace jam::px