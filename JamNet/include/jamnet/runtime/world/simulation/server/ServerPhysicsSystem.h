#pragma once


#include <jampx/PhysicsTypes.h>
#include <entt/fwd.hpp>
#include <vector>
#include <span>


namespace jam::px
{
	class PhysicsFacade;
}

namespace jam::net
{
	class ServerWorld;

	class ServerPhysicsSystem
	{
    public:
		ServerPhysicsSystem(ServerWorld& world, entt::registry& registry, px::PhysicsFacade* physics);

        void							Init();
        void							Tick();

        bool							SpawnActor(entt::entity e, const px::SpawnDesc& desc) const;
        bool							DespawnActor(entt::entity e) const;
        std::span<const entt::entity>	GetLastActiveEntities()    const { return m_lastActiveEntities; }
        std::span<const uint64>			GetLastAppliedInputUsers() const { return m_lastAppliedInputUsers; }

    private:
        void							ApplyInputs();
		void							Simulate();

        void							SyncActiveTransforms();
        void							SyncTransforms() const;
        void							HandlePhysicsEvents() const;

    private:
		ServerWorld&						m_world;
        entt::registry&                     m_registry;
        px::PhysicsFacade*                  m_physics           = nullptr;

		uint32                              m_completedTickCount = 0;

        std::vector<entt::entity>			m_lastActiveEntities;
        std::vector<uint64>                 m_lastAppliedInputUsers;
	};
}
