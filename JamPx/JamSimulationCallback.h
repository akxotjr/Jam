#pragma once

class JamSimulationCallback : public physx::PxSimulationEventCallback
{
public:
    void onContact(const physx::PxContactPairHeader& header, const physx::PxContactPair* pairs, physx::PxU32 count) override;
    void onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count) override;
    void onSleep(physx::PxActor** actors, physx::PxU32 count) override;
    void onWake(physx::PxActor** actors, physx::PxU32 count) override;
    void onConstraintBreak(physx::PxConstraintInfo* constraints, physx::PxU32 count) override;
};


class JamContactModifyCallback : public physx::PxContactModifyCallback
{
public:
	void onContactModify(physx::PxContactModifyPair* const pairs, physx::PxU32 count) override;
};

class JamCCDContactModifyCallback : public physx::PxCCDContactModifyCallback
{
public:
    void onCCDContactModify(physx::PxContactModifyPair* const pairs, physx::PxU32 count) override;
};

class JamBroadPhaseCallback : public physx::PxBroadPhaseCallback
{
public:
    void onObjectOutOfBounds(physx::PxShape& shape, physx::PxActor& actor) override;
};

class JamFilterCallback : public physx::PxSimulationFilterCallback
{
public:
    physx::PxFilterFlags	pairFound(physx::PxU64 pairID,
        physx::PxFilterObjectAttributes attributes0, physx::PxFilterData filterData0, const physx::PxActor* a0, const physx::PxShape* s0,
        physx::PxFilterObjectAttributes attributes1, physx::PxFilterData filterData1, const physx::PxActor* a1, const physx::PxShape* s1,
        physx::PxPairFlags& pairFlags) override;

    void			        pairLost(physx::PxU64 pairID,
        physx::PxFilterObjectAttributes attributes0, physx::PxFilterData filterData0,
        physx::PxFilterObjectAttributes attributes1, physx::PxFilterData filterData1,
        bool objectRemoved) override;
};
