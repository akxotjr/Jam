#include "pch.h"
#include "jampx/PhysicsCore.h"

#include <stdexcept>
#include <thread>


namespace jam::px
{

	void PhysicsCore::Init()
	{
        m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_allocator, m_errorCallback);
        if (!m_foundation) throw std::runtime_error("PhysicsCore::Init(), PxCreateFoundation failed");

        // PVD (must be created before PxCreatePhysics, and passed into it)
        m_pvd = PxCreatePvd(*m_foundation);
        if (m_pvd)
        {
            // PVD 앱 기본 포트가 5425
            m_pvdTransport = physx::PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
            if (m_pvdTransport)
            {
                const physx::PxPvdInstrumentationFlags flags = physx::PxPvdInstrumentationFlag::eALL; // debug/visualize용은 ALL이 편함

                m_pvd->connect(*m_pvdTransport, flags);
            }
        }

        PxTolerancesScale scale{};
        m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, scale, true, m_pvd);
        if (!m_physics) throw std::runtime_error("PhysicsCore::Init(), PxCreatePhysics failed");

        const unsigned threads = std::max(1u, std::thread::hardware_concurrency() / 2);
        m_dispatcher = physx::PxDefaultCpuDispatcherCreate(threads);
        if (!m_dispatcher) throw std::runtime_error("PhysicsCore::Init(), PxDefaultCpuDispatcherCreate failed");

	}

	void PhysicsCore::Shutdown()
	{
        if (m_dispatcher) { m_dispatcher->release(); m_dispatcher = nullptr; }
        if (m_physics)    { m_physics->release();    m_physics = nullptr; }

        if (m_pvd)
        {
            m_pvd->disconnect();
            m_pvd->release();
            m_pvd = nullptr;
        }
        if (m_pvdTransport)
        {
            m_pvdTransport->release();
            m_pvdTransport = nullptr;
        }

        if (m_foundation) { m_foundation->release(); m_foundation = nullptr; }
	}

	PxScene* PhysicsCore::CreateScene() const
	{
        PxSceneDesc desc = MakeDefaultSceneDesc();
        auto* sc = m_physics->createScene(desc);
        if (!sc) throw std::runtime_error("PhyiscsCore::CreateScene(), PxPhysics::createScene failed");
        return sc;
	}

	PxScene* PhysicsCore::CreateScene(const PxSceneDesc& overrideDesc) const
	{
        PxSceneDesc desc = MakeDefaultSceneDesc();

        // 선택적 오버라이드 (필요한 것만 명시적으로 반영)
        if (overrideDesc.cpuDispatcher) desc.cpuDispatcher = overrideDesc.cpuDispatcher;
        if (overrideDesc.filterShader)  desc.filterShader  = overrideDesc.filterShader;

        desc.gravity = overrideDesc.gravity;
        desc.flags   = overrideDesc.flags;

        auto* sc = m_physics->createScene(desc);
        if (!sc) throw std::runtime_error("PhysicsCore::CreateScene(overrideDesc), PxPhysics::createScene failed");
        return sc;
	}

	PxSceneDesc PhysicsCore::MakeDefaultSceneDesc() const
	{
        PxTolerancesScale scale{};
        PxSceneDesc desc(scale);

        desc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
        desc.cpuDispatcher = m_dispatcher;
        desc.filterShader = SimulationFilterShader<>;
        desc.flags |= PxSceneFlag::eENABLE_ACTIVE_ACTORS | PxSceneFlag::eENABLE_CCD | PxSceneFlag::eENABLE_STABILIZATION;

        return desc;
	}
}
