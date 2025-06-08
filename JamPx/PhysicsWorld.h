#pragma once


namespace jam::px
{
    class PhysicsWorld
    {
    public:
        void                    Init();
        void                    Shutdown();

        void                    Simulation(float dt);

        void                    AddActor(PxActor* actor);
        void                    RemoveActor(PxActor* actor);

        bool                    Raycast();
        bool                    Overlap();

        bool                    OverlapGround();

        PxScene*                GetScene() { return m_scene; }

    private:
        PxScene*                m_scene = nullptr;
    };

}

