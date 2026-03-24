#pragma once
#include <variant>


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
	struct ActorTemplateTag;


	using MaterialHandle				= Fnv1aHandle<MaterialTag, uint32>;
	using MeshHandle					= Fnv1aHandle<MeshTag, uint32>;
	using ShapeHandle					= Fnv1aHandle<ShapeTag, uint32>;
		
	using DynamicBodyHandle				= Fnv1aHandle<DynamicBodyTag, uint32>;
	using CCTBodyHandle					= Fnv1aHandle<CCTBodyTag, uint32>;

	using CharacterMoveConfigHandle		= Fnv1aHandle<CharacterMoveConfigTag, uint32>;
	using KinematicDriverConfigHandle	= Fnv1aHandle<KinematicDriverConfigTag, uint32>;
	using ProjectileConfigHandle		= Fnv1aHandle<ProjectileConfigTag, uint32>;

	using RigidBehaviorHandle			= std::variant<std::monostate, KinematicDriverConfigHandle, ProjectileConfigHandle>;

	using TemplateHandle				= Fnv1aHandle<ActorTemplateTag, uint32>;


	enum class eAssetHandleKind : uint8
	{
		None = 0,
		Template,
		Material,
		Mesh,
		Shape,
		DynamicBody,
		CCTBody,
		CharacterMoveConfig,
		KinematicDriverConfig,
		ProjectileConfig,
	};

	struct AssetHandle
	{
		eAssetHandleKind kind = eAssetHandleKind::None;
		uint32 v = 0;

		bool IsValid() const { return kind != eAssetHandleKind::None && v != 0; }
	};


	template<class THanlde>
	struct AssetHandleTraits;

	template<> struct AssetHandleTraits<TemplateHandle>					{ static constexpr eAssetHandleKind Kind = eAssetHandleKind::Template; };
	template<> struct AssetHandleTraits<MaterialHandle>					{ static constexpr eAssetHandleKind Kind = eAssetHandleKind::Material; };
	template<> struct AssetHandleTraits<MeshHandle>						{ static constexpr eAssetHandleKind Kind = eAssetHandleKind::Mesh; };
	template<> struct AssetHandleTraits<ShapeHandle>					{ static constexpr eAssetHandleKind Kind = eAssetHandleKind::Shape; };
	template<> struct AssetHandleTraits<DynamicBodyHandle>				{ static constexpr eAssetHandleKind Kind = eAssetHandleKind::DynamicBody; };
	template<> struct AssetHandleTraits<CCTBodyHandle>					{ static constexpr eAssetHandleKind Kind = eAssetHandleKind::CCTBody; };
	template<> struct AssetHandleTraits<CharacterMoveConfigHandle>		{ static constexpr eAssetHandleKind Kind = eAssetHandleKind::CharacterMoveConfig; };
	template<> struct AssetHandleTraits<KinematicDriverConfigHandle>	{ static constexpr eAssetHandleKind Kind = eAssetHandleKind::KinematicDriverConfig; };
	template<> struct AssetHandleTraits<ProjectileConfigHandle>			{ static constexpr eAssetHandleKind Kind = eAssetHandleKind::ProjectileConfig; };

	template<class THandle>
	inline AssetHandle ToAssetHandle(const THandle& h)
	{
		return AssetHandle{ AssetHandleTraits<THandle>::Kind, h.v };
	}

	template<class THandle>
	inline std::optional<THandle> TryAs(const AssetHandle& h)
	{
		if (h.kind != AssetHandleTraits<THandle>::Kind || h.v == 0)
			return std::nullopt;
		return THandle::FromU32(h.v);
	}

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


	struct RigidBodyDef
	{
		std::vector<ShapeHandle>		shapes;				// at least 1
		DynamicBodyHandle				dynamic	 = {};		// only meaningful if moitionType is Dynamic/Kinematic
		RigidBehaviorHandle				behavior = {};		// only meaningful if moitionType is Kinematic or actorType is Projectile

		bool HasBehavior() const { return !std::holds_alternative<std::monostate>(behavior); }

		template<class T>
		const T* GetBehavior() const { return std::get_if<T>(&behavior); }
	};

	struct CharacterBodyDef
	{
		CCTBodyHandle					cct				= {};
		std::vector<ShapeHandle>		hitboxes;
		eCharacterControlType			controllerType	= eCharacterControlType::Player;
		CharacterMoveConfigHandle		moveConfig		= {};

		bool operator==(const CharacterBodyDef&) const = default;
		bool HasHitbox() const { return !hitboxes.empty(); }
	};


	struct ActorTemplateDef
	{
		std::string			name;
		eActorType			actorType			= eActorType::Generic;
		eBodyType			bodyType			= eBodyType::Rigid;
		eMotionType			motionType			= eMotionType::Static;
		MotionFlag::Flags	motionFlags			= MotionFlag::NONE;

		eSpawnPolicy		spawnPolicy			= eSpawnPolicy::Both;
		bool				allowReplication	= true;

		std::variant<RigidBodyDef, CharacterBodyDef> body;

		bool IsRigid()		const noexcept { return std::holds_alternative<RigidBodyDef>(body); }
		bool IsCharacter()	const noexcept { return std::holds_alternative<CharacterBodyDef>(body); }
	};


	struct PhysicsAsset
	{
		int32 version = 1;

		std::unordered_map<MaterialHandle, MaterialDef>								materials;
		std::unordered_map<MeshHandle, MeshDef>										meshes;
		std::unordered_map<ShapeHandle, ShapeDef>									shapes;

		std::unordered_map<DynamicBodyHandle, DynamicBodyDef>						dynBodies;
		std::unordered_map<CCTBodyHandle, CCTBodyDef>								cctBodies;
		std::unordered_map<CharacterMoveConfigHandle, CharacterMoveConfig>			charMoveConfigs;
		std::unordered_map<KinematicDriverConfigHandle, KinematicDriverConfig>		kinematicDriverConfigs;
		std::unordered_map<ProjectileConfigHandle, ProjectileConfig>				projectileConfigs;

		std::unordered_map<TemplateHandle, ActorTemplateDef>						templates;
	};



	struct PhysicsLevelInstanceDef
	{
		uint32									levelActorId = 0;		// stable cross server/client id
		std::string								templateName;
		PxTransform								pose{ physx::PxIdentity };
		RigidSpawnOverrides						overrides{};
	};

	struct PhysicsLevelLayerDef
	{
		std::string								name;
		bool									enabled = true;
		std::vector<PhysicsLevelInstanceDef>	instances;
	};

	struct PhysicsLevelAsset
	{
		int32									version = 1;
		std::string								sceneName;
		std::vector<PhysicsLevelLayerDef>		layers;
	};


} // namespace jam::px