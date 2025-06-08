#pragma once

namespace jam::px
{
	struct ColliderInfo
	{
		enum class Type { Box, Capsule, Sphere, Plane };

		Type type;

		union
		{
			struct { float radius, halfHeight; } capsule;
			struct { float radius; } sphere;
			struct { float hx, hy, hz; } box;
		};

		PxVec3 localOffset = PxVec3(0, 0, 0);
		PxQuat localRotation = PxQuat(0, 0, 0, 1);

		static ColliderInfo MakeCapsule(float radius, float halfHeight)
		{
			ColliderInfo info;
			info.type = Type::Capsule;
			info.capsule = { radius, halfHeight };
			return info;
		}

		static ColliderInfo MakeSphere(float radius)
		{
			ColliderInfo info;
			info.type = Type::Sphere;
			info.sphere = { radius };
			return info;
		}

		static ColliderInfo MakeBox(float hx, float hy, float hz)
		{
			ColliderInfo info;
			info.type = Type::Box;
			info.box = { hx, hy, hz };
			return info;
		}

		static ColliderInfo MakePlane()
		{
			ColliderInfo info;
			info.type = Type::Plane;
			return info;
		}
	};



	class PhysicsManager
	{
	public:
		DECLARE_SINGLETON(PhysicsManager)

		void								Init();
		void								Shutdown();

		PxScene*							CreateDefaultScene();
		PxScene*							CreateScene(const PxSceneDesc& sceneDesc);

		PxMaterial*							GetDefaultMaterial();
		PxMaterial*							CreateMaterial(PxReal staticFriction, PxReal dynamicFriction, PxReal restitution);


		PxShape*							CreateShape(const ColliderInfo& info);


		PxRigidStatic*						CreateRigidStatic(const PxVec3& position, const PxQuat& rotation);
		PxActor*							CreateRigidDynamic(const PxVec3& position, const PxQuat& rotation);


	private:
		PxFoundation*						m_pxFoundation = nullptr;
		PxDefaultAllocator					m_allocatorCallback;
		PxDefaultErrorCallback				m_errorCallback;

		PxPhysics*							m_pxPhysics = nullptr;

		PxMaterial*							m_defaultMaterial = nullptr;
	};
}


