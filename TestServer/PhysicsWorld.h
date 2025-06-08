#pragma once

using namespace physx;

class PhysicsWorld
{
public:
    void                    Init();
    void                    Shutdown();

    void                    Simulation(float dt);

    void                    AddActor(PxActor* actor);
    void                    RemoveActor(PxActor* actor);

    PxScene*                GetScene() { return m_scene; }

private:
    PxScene*                m_scene = nullptr;
};

