#pragma once

#include <jambase/JamMacro.h>

#include "jampx/PhysXIncludes.h"
#include "jampx/PhysXTypes.h"

namespace jam::px
{
	class PhysicsCore final
	{
		DECLARE_SINGLETON(PhysicsCore)

	public:
		void							Init();
		void							Shutdown();

		PxScene*						CreateScene() const;
		PxScene*						CreateScene(const PxSceneDesc& overrideDesc) const;

		PxPhysics*						Physics()	 const { return m_physics; }
		PxFoundation*					Foundation() const { return m_foundation; }
		physx::PxDefaultCpuDispatcher*	Dispatcher() const { return m_dispatcher; }

		PxSceneDesc						MakeDefaultSceneDesc() const;

	private:
		physx::PxDefaultAllocator		m_allocator;
		physx::PxDefaultErrorCallback	m_errorCallback;
		PxFoundation*					m_foundation	= nullptr;
		PxPhysics*						m_physics		= nullptr;
		
		physx::PxDefaultCpuDispatcher*	m_dispatcher	= nullptr;

		// PVD
		physx::PxPvd*					m_pvd			= nullptr;
		physx::PxPvdTransport*			m_pvdTransport	= nullptr;
	};

}


#define PHYSICS_CORE			jam::px::PhysicsCore::Instance()
#define PHYSICS_CORE_INIT()		jam::px::PhysicsCore::Instance().Init()
#define PHYSICS_CORE_SHUTDOWN()	jam::px::PhysicsCore::Instance().Shutdown()

#define PX_PHYSICS				jam::px::PhysicsCore::Instance().Physics()
#define PX_FOUNDATION			jam::px::PhysicsCore::Instance().Foundation()
#define PX_DISPATCHER			jam::px::PhysicsCore::Instance().Dispatcher()
