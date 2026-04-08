#pragma once

#include <jambase/Fnv1a.h>

#include "jampx/PhysicsUtils.h"

namespace jam::px
{
    struct SimCategory
    {
        enum Enum : PxU32
        {
            NONE            = 0,
            WORLD_STATIC    = 1u << 0,
            WORLD_DYNAMIC   = 1u << 1,
            CHARACTER       = 1u << 2,
            PROJECTILE      = 1u << 3,
            SENSOR          = 1u << 4,
            ALL             = 0xFFFFFFFFu
        };

        using Flags = jam::FlagsT<Enum, PxU32>;
    };


    static constexpr bool PassSimCategory(PxU32 aCategory, PxU32 aMask, PxU32 bCategory, PxU32 bMask)
    {
        if ((aCategory & bMask) == 0) return false;
        if ((bCategory & aMask) == 0) return false;
        return true;
    }


    struct SimUser
    {
        enum Enum : PxU32
        {
            NONE                     = 0,

            // --- Touch notification (onContact or onTrigger) ---

            NOTIFY_TOUCH_FOUND       = 1u << 0,
            NOTIFY_TOUCH_LOST        = 1u << 1,
            NOTIFY_TOUCH_PERSISTS    = 1u << 2,      // not supported for trigger
            NOTIFY_TOUCH_CCD         = 1u << 3,      // requires CCD detect & non-trigger

            // --- Contact modification ---

            MODIFY_CONTACTS          = 1u << 4,      // PxPairFlag::eMODIFY_CONTACTS

            // --- Conatct points in reports (requires some notify/threshold flag to be meaningful) ---

            NOTIFY_CONTACT_POINTS    = 1u << 5,      // PxPairFlag::eNOTIFY_CONTACT_POINTS

            // --- Threshold force notification (contact only) ---

            THRESHOLD_FORCE_FOUND    = 1u << 6,
            THRESHOLD_FORCE_PERSISTS = 1u << 7,
            THRESHOLD_FORCE_LOST     = 1u << 8,

            PRE_SOLVER_VELOCITY      = 1u << 9,
            POST_SOLVER_VELOCITY     = 1u << 10,
            CONTACT_EVENT_POSE       = 1u << 11,

            // --- Policy toggles (optional) ---
            // If you want: "always request reports even if pairFlags doesn't include contact default"
            // Keep as reserved for future.

            RESERVED_12             = 1u << 12,
            RESERVED_13             = 1u << 13,
            RESERVED_14             = 1u << 14,
            RESERVED_15             = 1u << 15,
        };

        using Flags = jam::FlagsT<Enum, PxU32>;


        static constexpr PxU32 k_touchMask =
            NOTIFY_TOUCH_FOUND | NOTIFY_TOUCH_LOST | NOTIFY_TOUCH_PERSISTS | NOTIFY_TOUCH_CCD;

        static constexpr PxU32 k_thresholdMask =
            THRESHOLD_FORCE_FOUND | THRESHOLD_FORCE_PERSISTS | THRESHOLD_FORCE_LOST;

        static constexpr PxU32 k_reportExtrasMask =
            PRE_SOLVER_VELOCITY | POST_SOLVER_VELOCITY | CONTACT_EVENT_POSE;

        static constexpr PxU32 k_needsDetectMask =
            k_touchMask | MODIFY_CONTACTS | NOTIFY_CONTACT_POINTS | k_thresholdMask | k_reportExtrasMask;

        static constexpr PxU32 k_contactOnlyMask =
            NOTIFY_TOUCH_PERSISTS | NOTIFY_TOUCH_CCD | MODIFY_CONTACTS | NOTIFY_CONTACT_POINTS | k_thresholdMask | k_reportExtrasMask;

        // Trigger pairs: 
        // - persists not supported.
        // - CCD touch notify not supported.
        // - modify contacts not meaningful.
        // - threshold/report extras not meaningful.
        static constexpr PxU32 k_triggerDisallowedMask =
            NOTIFY_TOUCH_PERSISTS | NOTIFY_TOUCH_CCD | MODIFY_CONTACTS | k_thresholdMask | k_reportExtrasMask;

        // If someone sets CONTACT_POINTS alone, we can either:
		// - auto add NOTIFY_TOUCH_FOUND (default choice), or
		// - drop CONTACT_POINTS.
		// We'll auto-add NOTIFY_TOUCH_FOUND by default (configurable).
        static constexpr PxU32 k_contactPointsNeedsSomethingMask = NOTIFY_CONTACT_POINTS;
    };


    enum class ePairKind : PxU8
    {
        Auto,
        ForceContact,
        ForceTrigger,
    };

    enum class eCCDMode : PxU8
    {
        Auto,               // prefer discrete unless SimUser asks for CCD touch notify
        PreferCCD,          // if possible, use CCD detect for contact pairs
        ForceDiscrete,      // never use CCD detect
        ForceCCD,           // use CCD detect (if impossible -> fallback : discrete)
    };



    struct PairFlagsBuildInput
    {
        PxFilterObjectAttributes    attrs0{};
        PxFilterData                fd0{};
        PxFilterObjectAttributes    attrs1{};
        PxFilterData                fd1{};

        ePairKind                   kind = ePairKind::Auto;

        // set true only if scene + relevant bodies use CCD
        bool                        enableCCDDetect = false;
        eCCDMode                    ccdMode = eCCDMode::Auto;

        // If CONTACT_POINTS is set alone, choose behavior:
        // - true  => auto add NOTIFY_TOUCH_FOUND
        // - false => drop CONTACT_POINTS
        bool                        autoAddNotifyForContactPoints = true;

        // If report extras(pre/post vel, pose) are set without any notify/threshold, choose behavior:
        // - true  => auto add NOTIFY_TOUCH_FOUND
        // - false => keep extras, but they not be delivered (usually pointless)
        bool                        autoAddNotifyForReportExtras = true;
    };


    inline bool IsTriggerPair(PxFilterObjectAttributes attrs0, PxFilterObjectAttributes attrs1)
    {
        return physx::PxFilterObjectIsTrigger(attrs0) || physx::PxFilterObjectIsTrigger(attrs1);
    }

    inline PxU32 ExtractSimUserBits(const PxFilterData& fd0, const PxFilterData& fd1) noexcept
    {
        return (fd0.word2 | fd1.word2);
    }


    struct PairFlagsBuilder
    {
        static PxPairFlags Build(const PairFlagsBuildInput& in)
        {
            const bool isTrigger =
                (in.kind == ePairKind::ForceTrigger) ? true :
                (in.kind == ePairKind::ForceContact) ? false :
                IsTriggerPair(in.attrs0, in.attrs1);

            PxU32 u = ExtractSimUserBits(in.fd0, in.fd1);

            if (isTrigger)
            {
                u &= ~SimUser::k_triggerDisallowedMask;
            }

            PxPairFlags flags = isTrigger ? PxPairFlag::eTRIGGER_DEFAULT : PxPairFlag::eCONTACT_DEFAULT;

            if (u & SimUser::NOTIFY_TOUCH_FOUND) flags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
            if (u & SimUser::NOTIFY_TOUCH_LOST)  flags |= PxPairFlag::eNOTIFY_TOUCH_LOST;

            if (!isTrigger)
            {
                if (u & SimUser::NOTIFY_TOUCH_PERSISTS) flags |= PxPairFlag::eNOTIFY_TOUCH_PERSISTS;
                if (u & SimUser::NOTIFY_TOUCH_CCD)      flags |= PxPairFlag::eNOTIFY_TOUCH_CCD;
            }

            // Modify
            if (!isTrigger && (u & SimUser::MODIFY_CONTACTS))
                flags |= PxPairFlag::eMODIFY_CONTACTS;

            // Threshold force (contact only)
            if (!isTrigger)
            {
                if (u & SimUser::THRESHOLD_FORCE_FOUND)    flags |= PxPairFlag::eNOTIFY_THRESHOLD_FORCE_FOUND;
                if (u & SimUser::THRESHOLD_FORCE_PERSISTS) flags |= PxPairFlag::eNOTIFY_THRESHOLD_FORCE_PERSISTS;
                if (u & SimUser::THRESHOLD_FORCE_LOST)     flags |= PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST;
            }

            // Contact points (contact report extra)
            if (!isTrigger && (u & SimUser::NOTIFY_CONTACT_POINTS))
                flags |= PxPairFlag::eNOTIFY_CONTACT_POINTS;

            // Report stream extras (contact only)
            if (!isTrigger)
            {
                if (u & SimUser::PRE_SOLVER_VELOCITY)  flags |= PxPairFlag::ePRE_SOLVER_VELOCITY;
                if (u & SimUser::POST_SOLVER_VELOCITY) flags |= PxPairFlag::ePOST_SOLVER_VELOCITY;
                if (u & SimUser::CONTACT_EVENT_POSE)   flags |= PxPairFlag::eCONTACT_EVENT_POSE;
            }

            // Safety auto-add: if user asked for contact points or report extras without any trigger to emit reports
            // We treat "touch notify" or "threshold force notify" as "something that triggers a report".
            // If none present, CONTACT_POINTS / stream extras are typically useless.
            if (!isTrigger)
            {
                const bool hasSomeNotify = (flags & (PxPairFlag::eNOTIFY_TOUCH_FOUND                 |
                                                        PxPairFlag::eNOTIFY_TOUCH_LOST               |
                                                        PxPairFlag::eNOTIFY_TOUCH_PERSISTS           |
                                                        PxPairFlag::eNOTIFY_TOUCH_CCD                |
                                                        PxPairFlag::eNOTIFY_THRESHOLD_FORCE_FOUND    |
                                                        PxPairFlag::eNOTIFY_THRESHOLD_FORCE_PERSISTS |
                                                        PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST)) != PxPairFlags();

                const bool wantsContactPoints = flags.isSet(PxPairFlag::eNOTIFY_CONTACT_POINTS);
                const bool wantsStreamExtras = (flags & (PxPairFlag::ePRE_SOLVER_VELOCITY | PxPairFlag::ePOST_SOLVER_VELOCITY | PxPairFlag::eCONTACT_EVENT_POSE)) != PxPairFlags();

                if (!hasSomeNotify)
                {
                    if (wantsContactPoints && in.autoAddNotifyForContactPoints)
                    {
                        flags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
                    }
                    else if (wantsContactPoints && !in.autoAddNotifyForContactPoints)
                    {
                        flags.clear(PxPairFlag::eNOTIFY_CONTACT_POINTS);
                    }

                    if (wantsStreamExtras && in.autoAddNotifyForReportExtras)
                    {
                        flags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
                    }
                    // else: keep extras, but they may not be delivered (rarely useful)
                }
            }

            // DETECT prerequisites:
            // If any notify/modify/threshold/stream requested, ensure detect discrete contact.
            const bool wantsDetect =
                (u & SimUser::k_needsDetectMask) != 0 || (flags & (PxPairFlag::eNOTIFY_TOUCH_FOUND              |
                                                                   PxPairFlag::eNOTIFY_TOUCH_LOST               |
                                                                   PxPairFlag::eNOTIFY_TOUCH_PERSISTS           |
                                                                   PxPairFlag::eNOTIFY_TOUCH_CCD                |
                                                                   PxPairFlag::eMODIFY_CONTACTS                 |
                                                                   PxPairFlag::eNOTIFY_CONTACT_POINTS           |
                                                                   PxPairFlag::eNOTIFY_THRESHOLD_FORCE_FOUND    |
                                                                   PxPairFlag::eNOTIFY_THRESHOLD_FORCE_PERSISTS |
                                                                   PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST     |
                                                                   PxPairFlag::ePRE_SOLVER_VELOCITY             |
                                                                   PxPairFlag::ePOST_SOLVER_VELOCITY            |
                                                                   PxPairFlag::eCONTACT_EVENT_POSE)) != PxPairFlags();

            if (wantsDetect)
            {
                // Default: ensure discrete detect
                flags |= PxPairFlag::eDETECT_DISCRETE_CONTACT;
            }

            // CCD resolution:
            // If user asked for NOTIFY_TOUCH_CCD or engine policy wants CCD detect, try to enable CCD detect.
            // If impossible, fall back to discrete. Also remove CCD notify if CCD detect isn't enabled.
            if (!isTrigger)
            {
                const bool wantsCcdNotify = flags.isSet(PxPairFlag::eNOTIFY_TOUCH_CCD);
                const bool wantsCcdByMode = (in.ccdMode == eCCDMode::PreferCCD) || (in.ccdMode == eCCDMode::ForceCCD);

                bool enableCcdDetect = false;

                if (in.enableCCDDetect)
                {
                    if (in.ccdMode == eCCDMode::ForceDiscrete)
                    {
                        enableCcdDetect = false;
                    }
                    else if (in.ccdMode == eCCDMode::ForceCCD)
                    {
                        enableCcdDetect = true;
                    }
                    else if (wantsCcdNotify || wantsCcdByMode)
                    {
                        enableCcdDetect = true;
                    }
                }
                else
                {
                    // CCD not available in current configuration.
                    enableCcdDetect = false;
                }

                if (enableCcdDetect)
                {
                    flags |= PxPairFlag::eDETECT_CCD_CONTACT;
                    // For CCD notify to work, CCD detect must be present (we ensured it).
                    // Keep eNOTIFY_TOUCH_CCD if requested.
                }
                else
                {
                    // No CCD detect => CCD notify meaningless
                    flags.clear(PxPairFlag::eDETECT_CCD_CONTACT);
                    flags.clear(PxPairFlag::eNOTIFY_TOUCH_CCD);
                }
            }
            else
            {
                // Trigger pair: clear CCD detect/notify and other contact-only extras
                flags.clear(PxPairFlag::eDETECT_CCD_CONTACT);
                flags.clear(PxPairFlag::eNOTIFY_TOUCH_CCD);
                flags.clear(PxPairFlag::eNOTIFY_TOUCH_PERSISTS);
                flags.clear(PxPairFlag::eMODIFY_CONTACTS);
                flags.clear(PxPairFlag::eNOTIFY_THRESHOLD_FORCE_FOUND);
                flags.clear(PxPairFlag::eNOTIFY_THRESHOLD_FORCE_PERSISTS);
                flags.clear(PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST);
                flags.clear(PxPairFlag::ePRE_SOLVER_VELOCITY);
                flags.clear(PxPairFlag::ePOST_SOLVER_VELOCITY);
                flags.clear(PxPairFlag::eCONTACT_EVENT_POSE);
                flags.clear(PxPairFlag::eSOLVE_CONTACT); // triggers don't solve
            }

            // Final small sanity:
            // - If no notify/threshold/modify/points/extras requested, you can keep default as-is.
            // - DETECT_DISCRETE_CONTACT is harmless; leave it if we added it.
            // - You may optionally remove DETECT_DISCRETE_CONTACT for pure "solve only" pairs, but we don't.
            return flags;

        }
    };


    inline PxPairFlags BuildSafePairFlags(
        PxFilterObjectAttributes attrs0, const PxFilterData& fd0,
        PxFilterObjectAttributes attrs1, const PxFilterData& fd1,
        ePairKind kind = ePairKind::Auto)
    {
        PairFlagsBuildInput in{};
        in.attrs0 = attrs0; in.fd0 = fd0;
        in.attrs1 = attrs1; in.fd1 = fd1;
        in.kind   = kind;

        return PairFlagsBuilder::Build(in);
    }


    struct SimFD
    {
        SimCategory::Flags      category{};  // word0 
        SimCategory::Flags      mask{};      // word1 
        SimUser::Flags          userFlags{}; // word2 
        PxU32                   custom = 0;  // word3 

        static SimFD FromPx(const PxFilterData& fd)
        {
            SimFD sfd{};
            sfd.category  = static_cast<SimCategory::Flags>(fd.word0);
            sfd.mask      = static_cast<SimCategory::Flags>(fd.word1);
            sfd.userFlags = static_cast<SimUser::Flags>(fd.word2);
            sfd.custom    = fd.word3;

            return sfd;
        }

        PxFilterData ToPx() const
        {
            PxFilterData fd;
            fd.word0 = category.bits();
            fd.word1 = mask.bits();
            fd.word2 = userFlags.bits();
            fd.word3 = custom;
            return fd;
        }
    };

    inline void HashAppend(jam::Fnv1a32& h, const SimFD& fd) noexcept
    {
        HashAppend(h, fd.category.bits());
        HashAppend(h, fd.mask.bits());
        HashAppend(h, fd.userFlags.bits());
        HashAppend(h, fd.custom);
    }



    struct DefaultSimPolicy
    {
        static void ConfigurePairFlags(
            PxFilterObjectAttributes attrs0, const PxFilterData& fd0,
            PxFilterObjectAttributes attrs1, const PxFilterData& fd1,
            PxPairFlags& pairFlags)
        {
            PairFlagsBuildInput in{};
            in.attrs0 = attrs0; in.fd0 = fd0;
            in.attrs1 = attrs1; in.fd1 = fd1;

            pairFlags = PairFlagsBuilder::Build(in);
        }

        static PxFilterFlags PostDecision(
            PxFilterObjectAttributes, const PxFilterData&,
            PxFilterObjectAttributes, const PxFilterData&)
        {
            return PxFilterFlag::eDEFAULT;
        }
    };

    template<class PolicyT = DefaultSimPolicy>
    PxFilterFlags SimulationFilterShader(
        PxFilterObjectAttributes attrs0, PxFilterData fd0,
        PxFilterObjectAttributes attrs1, PxFilterData fd1,
        PxPairFlags& pairFlags,
        const void*, PxU32)
    {
        if (!PassSimCategory(fd0.word0, fd0.word1, fd1.word0, fd1.word1))
            return PxFilterFlag::eKILL;

        PolicyT::ConfigurePairFlags(attrs0, fd0, attrs1, fd1, pairFlags);
        return PolicyT::PostDecision(attrs0, fd0, attrs1, fd1);
    }



    enum class eSimEventType : uint8
    {
        None,

        ContactFound,
        ContactLost,
        ContactPersists,
        ContactCcd,

        ThresholdForceFound,
        ThresholdForcePersists,
        ThresholdForceLost,

        TriggerFound,
        TriggerLost,

        ProjectileHit,
        ProjectileLifetimeExpired,

        //(optional)
        //AdvancePose, 
    };

    struct SimContactPoint
    {
        PxVec3      position   = PxVec3(physx::PxZero);
        PxVec3      normal     = PxVec3(physx::PxZero);
        PxReal      separation = 0.f;
    };

    struct SimEvent
    {
        eSimEventType       type        = eSimEventType::None;

        // ---- contact pair identity(ObjectId) ----

        ObjectId            contact0    = INVALID_OBJ_ID;
        ObjectId            contact1    = INVALID_OBJ_ID;

        // ---- trigger pair identity(ObjectId) ----

        ObjectId            trigger0    = INVALID_OBJ_ID;
        ObjectId            trigger1    = INVALID_OBJ_ID;

        // ---- contact extras ----

        PxReal              contactImpulseSum = 0.f;
        PxU32               contactPointCount = 0;
        SimContactPoint     contactPoints[8]{}; // 고정 크기(과도한 할당 방지)

        // ---- onAdvance ----
		//(optional)
        //ObjectId            body        = INVALID_OBJ_ID;
        //PxTransform         pose        = PxTransform(PxIdentity);
    };

    struct SimulationEventCallback final : PxSimulationEventCallback
    {
        // 비용 제어: contact points는 최대 몇 개 뽑을지
        PxU32                   maxExtractContacts = 8;
        std::vector<SimEvent>   events;
        std::vector<ObjectId>   advanceActive;

        void Clear()
        {
            events.clear();
        }

        std::vector<SimEvent> ConsumeEvents()
        {
            std::vector<SimEvent> out;
            out.swap(events);
            return out;
        }

        std::vector<ObjectId> ConsumeActiveList()
        {
            std::vector<ObjectId> out;
            out.swap(advanceActive);
            return out;
        }

        // ---- unused by default ----

        void onConstraintBreak(physx::PxConstraintInfo*, PxU32) override {}
        void onWake(PxActor**, PxU32) override {}
        void onSleep(PxActor**, PxU32) override {}

        // ---- Contact ----

        void onContact(const PxContactPairHeader& header, const PxContactPair* pairs, PxU32 nbPairs) override
        {
            const ObjectId oid0 = GetObjectId(header.actors[0]);
            const ObjectId oid1 = GetObjectId(header.actors[1]);

            for (PxU32 i = 0; i < nbPairs; ++i)
            {
                const PxContactPair& cp = pairs[i];

                const bool removed0 = cp.flags.isSet(PxContactPairFlag::eREMOVED_SHAPE_0);
                const bool removed1 = cp.flags.isSet(PxContactPairFlag::eREMOVED_SHAPE_1);

                auto pushEvent = [&](eSimEventType t)
                    {
                        SimEvent e{};
                        e.type     = t;
                        e.contact0 = oid0;
                        e.contact1 = oid1;

                        // contact points 추출은 "요청된 페어"에서만 실제로 유효/의미가 있음
                        // (PairFlagsBuilder에서 eNOTIFY_CONTACT_POINTS를 켠 경우)
                        if (!removed0 && !removed1 && (cp.events & PxPairFlag::eNOTIFY_CONTACT_POINTS))
                        {
                            PxContactPairPoint pts[16];
                            const PxU32 cap = (maxExtractContacts < 16u) ? maxExtractContacts : 16u;
                            const PxU32 n   = cp.extractContacts(pts, cap);

                            e.contactPointCount = (n <= 8u) ? n : 8u;
                            for (PxU32 k = 0; k < e.contactPointCount; ++k)
                            {
                                e.contactPoints[k].position   = pts[k].position;
                                e.contactPoints[k].normal     = pts[k].normal;
                                e.contactPoints[k].separation = pts[k].separation;
                            }
                        }

                        // threshold-force나 impulse가 필요하면 여기서 추가 계산/추출 가능.
                        // (정확한 impulse/force는 설정과 스트림에 따라 제한이 있어서,
                        //  엔진에서 필요해질 때만 확장하는 걸 추천)
                		events.push_back(e);
                    };

                if (cp.events & PxPairFlag::eNOTIFY_TOUCH_FOUND)              pushEvent(eSimEventType::ContactFound);
                if (cp.events & PxPairFlag::eNOTIFY_TOUCH_LOST)               pushEvent(eSimEventType::ContactLost);
                if (cp.events & PxPairFlag::eNOTIFY_TOUCH_PERSISTS)           pushEvent(eSimEventType::ContactPersists);
                if (cp.events & PxPairFlag::eNOTIFY_TOUCH_CCD)                pushEvent(eSimEventType::ContactCcd);
                if (cp.events & PxPairFlag::eNOTIFY_THRESHOLD_FORCE_FOUND)    pushEvent(eSimEventType::ThresholdForceFound);
                if (cp.events & PxPairFlag::eNOTIFY_THRESHOLD_FORCE_PERSISTS) pushEvent(eSimEventType::ThresholdForcePersists);
                if (cp.events & PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST)     pushEvent(eSimEventType::ThresholdForceLost);
            }
        }


        // ---- Trigger ----

        void onTrigger(PxTriggerPair* pairs, PxU32 count) override
        {
            for (PxU32 i = 0; i < count; ++i)
            {
                const PxTriggerPair& tp = pairs[i];

                const bool removedTrigger = tp.flags.isSet(PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER);
                const bool removedOther   = tp.flags.isSet(PxTriggerPairFlag::eREMOVED_SHAPE_OTHER);

                const ObjectId oid0 = removedTrigger ? INVALID_OBJ_ID : GetObjectId(tp.triggerActor);
                const ObjectId oid1 = removedOther   ? INVALID_OBJ_ID : GetObjectId(tp.otherActor);

                auto pushEvent = [&](eSimEventType t)
                    {
                        SimEvent e{};
                        e.type      = t;
                        e.trigger0  = oid0;
                        e.trigger1  = oid1;

                        events.push_back(e);
                    };

                if (tp.status & PxPairFlag::eNOTIFY_TOUCH_FOUND) pushEvent(eSimEventType::TriggerFound);
                if (tp.status & PxPairFlag::eNOTIFY_TOUCH_LOST)  pushEvent(eSimEventType::TriggerLost);
            }
        }

        // ---- Advance ----

        void onAdvance(const PxRigidBody* const* bodyBuffer, const PxTransform* poseBuffer, const PxU32 count) override
        {
            for (PxU32 i = 0; i < count; ++i)
            {
                const ObjectId oid = GetObjectId(bodyBuffer[i]);
                if (oid == INVALID_OBJ_ID) continue;

                advanceActive.push_back(oid);

                //(optional)
                //SimEvent e{};
                //e.type = eSimEventType::AdvancePose;
                //e.body = oid;
                //e.pose = poseBuffer[i];

                //events.push_back(e);
            }
        }
    };


    static SimFD MakeSimFD(
        SimCategory::Flags  category,
        SimCategory::Flags  mask,
        SimUser::Flags      userFlags = SimUser::NONE,
        PxU32               custom = 0)
    {
        SimFD s{};
        s.category  = category;
        s.mask      = mask;
        s.userFlags = userFlags;
        s.custom    = custom;
        return s;
    }


} // namespace jam::px
