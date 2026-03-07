#pragma once


#include <jambase/EnumUtils.h>

#include <type_traits>

#include "jambase/Fnv1a.h"

namespace jam::px
{

    struct QueryCategory
    {
        enum Enum : PxU32
        {
            NONE        = 0,
            WORLD       = 1u << 0,
            CHARACTER   = 1u << 1,
            HITBOX      = 1u << 2,
            TRIGGER     = 1u << 3,
            ALL         = 0xFFFFFFFFu
        };

        using Flags = FlagsT<Enum>;
    };


    struct PackedId32
    {
        PxU32 v{ 0 };

        static constexpr PxU32 Pack(PxU16 teamId, PxU8 partId, PxU8 roleId)
        {
            return (static_cast<PxU32>(teamId) & 0xFFFFu)
                | ((static_cast<PxU32>(partId) & 0xFFu) << 16)
                | ((static_cast<PxU32>(roleId) & 0xFFu) << 24);
        }

        static constexpr PackedId32 Make(PxU16 teamId, PxU8 partId = 0, PxU8 roleId = 0)
        {
            return PackedId32{ Pack(teamId, partId, roleId) };
        }

        constexpr PxU16 Team() const { return static_cast<PxU16>(v & 0xFFFFu); }
        constexpr PxU8  Part() const { return static_cast<PxU8>((v >> 16) & 0xFFu); }
        constexpr PxU8  Role() const { return static_cast<PxU8>((v >> 24) & 0xFFu); }
    };


    struct QueryMeta
    {
        PxU32 v{ 0 };

        static constexpr PxU32 Pack(PxU8 channel, PxU8 sublayer, PxU16 tag)
        {
            return (static_cast<PxU32>(channel) & 0xFFu)
                | ((static_cast<PxU32>(sublayer) & 0xFFu) << 8)
                | ((static_cast<PxU32>(tag) & 0xFFFFu) << 16);
        }

        static constexpr QueryMeta Make(PxU8 channel = 0, PxU8 sublayer = 0, PxU16 tag = 0)
        {
            return QueryMeta{ Pack(channel, sublayer, tag) };
        }

        constexpr PxU8  Channel()  const { return static_cast<PxU8>(v & 0xFFu); }
        constexpr PxU8  Sublayer() const { return static_cast<PxU8>((v >> 8) & 0xFFu); }
        constexpr PxU16 Tag()      const { return static_cast<PxU16>((v >> 16) & 0xFFFFu); }
    };

    struct QuerySublayer
    {
        static constexpr PxU8 Default = 0;
        static constexpr PxU8 LOS     = 1;
    };

    struct ShapeQueryFlag
    {
        enum Enum : PxU32
        {
            NONE            = 0,
            IS_HEAD         = 1u << 0,
            PENETRABLE      = 1u << 1,
            NO_LOS_BLOCK    = 1u << 2,


            //---- World/Physics attrs ----

            WORLD_STATIC    = 1u << 8,
            WORLD_DYNAMIC   = 1u << 9,
            WORLD_KINEMATIC = 1u << 10,
            RIDEABLE        = 1u << 11,
        };

        using Flags = FlagsT<Enum>;
    };


    struct RequestQueryFlag
    {
        enum Enum : PxU32
        {
            NONE                = 0,
            IGNORE_TRIGGERS     = 1u << 0,
            IGNORE_SELF_ACTOR   = 1u << 1,
            IGNORE_SAME_TEAM    = 1u << 2,
            ACCEPT_PENETRABLE   = 1u << 3,


            //---- HitTypeMapMode ----

            MAP_MODE_MASK       = 3u << 8,
            MAP_DEFAULT         = 0u << 8,
            MAP_ALL_TOUCH       = 1u << 8,
            MAP_ALL_BLOCK       = 2u << 8,
        };

        using Flags = FlagsT<Enum>;
    };

    enum class eQueryHitMapMode : PxU8
    {
        Default,
        AllTouch,
        AllBlock,
    };



    inline bool IsTriggerAttrs(PxFilterObjectAttributes attrs)
    {
        return physx::PxFilterObjectIsTrigger(attrs);
    }


    constexpr eQueryHitMapMode GetHitMapMode(RequestQueryFlag::Flags flags)
    {
        const PxU32 mode = flags.bits() & RequestQueryFlag::MAP_MODE_MASK;
        if (mode == RequestQueryFlag::MAP_ALL_TOUCH) return eQueryHitMapMode::AllTouch;
        if (mode == RequestQueryFlag::MAP_ALL_BLOCK) return eQueryHitMapMode::AllBlock;

        return eQueryHitMapMode::Default;
    }

    struct QueryFD
    {
        QueryCategory::Flags        category{}; // word0 
        QueryMeta                   meta{};     // word1 
        PackedId32                  id{};       // word2 
        ShapeQueryFlag::Flags       flags{};    // word3 

        static QueryFD FromPx(const PxFilterData& fd)
        {
            QueryFD qfd{};
            qfd.category = static_cast<QueryCategory::Flags>(fd.word0);
            qfd.meta.v   = fd.word1;
            qfd.id.v     = fd.word2;
            qfd.flags    = static_cast<ShapeQueryFlag::Flags>(fd.word3);
            return qfd;
        }

        PxFilterData ToPx() const
        {
            PxFilterData fd;
            fd.word0 = category.bits();
            fd.word1 = meta.v;
            fd.word2 = id.v;
            fd.word3 = flags.bits();
            return fd;
        }
    };

    inline void HashAppend(jam::Fnv1a32& h, const QueryFD& fd) noexcept
    {
        HashAppend(h, fd.category.bits());
        HashAppend(h, fd.meta.v);
        HashAppend(h, fd.id.v);
        HashAppend(h, fd.flags.bits());
    }

    struct RequestQueryFD
    {
        QueryCategory::Flags        mask{};           // word0
        QueryMeta                   meta{};           // word1
        PackedId32                  id{};             // word2
        RequestQueryFlag::Flags     flags{};          // word3

        static RequestQueryFD FromPx(const PxFilterData& fd)
        {
            RequestQueryFD qrfd{};
            qrfd.mask   = static_cast<QueryCategory::Flags>(fd.word0);
            qrfd.meta.v = fd.word1;
            qrfd.id.v   = fd.word2;
            qrfd.flags  = static_cast<RequestQueryFlag::Flags>(fd.word3);

            return qrfd;
        }

        PxFilterData ToPx() const
        {
            PxFilterData fd;
            fd.word0 = mask.bits();
            fd.word1 = meta.v;
            fd.word2 = id.v;
            fd.word3 = flags.bits();
            return fd;
        }
    };

    constexpr bool PassQueryCategory(PxU32 shapeQueryCategoryBits, PxU32 requestedMaskBits)
    {
        return (shapeQueryCategoryBits & requestedMaskBits) != 0;
    }

    inline bool PassChannelAndSublayer(const QueryMeta& shapeMeta, const QueryMeta& reqMeta)
    {
        return shapeMeta.Channel() == reqMeta.Channel() && shapeMeta.Sublayer() == reqMeta.Sublayer();
    }



    // ============================================================
    //  Query HitType Mapping (category -> NONE/TOUCH/BLOCK)
    // ============================================================

    struct QueryHitTypeMap
    {
        PxQueryHitType::Enum world     = PxQueryHitType::eBLOCK;
        PxQueryHitType::Enum character = PxQueryHitType::eBLOCK;
        PxQueryHitType::Enum hitbox    = PxQueryHitType::eBLOCK;
        PxQueryHitType::Enum trigger   = PxQueryHitType::eNONE;
        PxQueryHitType::Enum other     = PxQueryHitType::eBLOCK;

        PxQueryHitType::Enum For(const PxU32 category) const
        {
            if (category & QueryCategory::HITBOX)       return hitbox;
            if (category & QueryCategory::CHARACTER)    return character;
            if (category & QueryCategory::WORLD)        return world;
            if (category & QueryCategory::TRIGGER)      return trigger;
            return other;
        }
    };


    static constexpr QueryHitTypeMap k_LOSQueryHitTypeMap{
        .world      = PxQueryHitType::eBLOCK,
        .character  = PxQueryHitType::eNONE,
        .hitbox     = PxQueryHitType::eNONE,
        .trigger    = PxQueryHitType::eNONE,
        .other      = PxQueryHitType::eNONE
    };

    struct QueryEvaluateContext
    {
        const RequestQueryFD&   rqfd;        // 쿼리 요청 측 FD (category-mask, meta, id, flags)
        const QueryFD&          qfd;         // 셰이프 측 FD (category, meta, id, flags)
        const PxShape*          shape = nullptr;
        const PxRigidActor*     actor = nullptr;
    };


    struct DefaultQueryPolicy
    {
        const PxRigidActor* selfActor = nullptr;

        bool AcceptCandidate(const QueryEvaluateContext& ctx) const
        {
            const RequestQueryFlag::Flags& rf  = ctx.rqfd.flags;
            const ShapeQueryFlag::Flags&   sqf = ctx.qfd.flags;

            if (rf.has_any(RequestQueryFlag::IGNORE_TRIGGERS) && (ctx.shape->getFlags() & PxShapeFlag::eTRIGGER_SHAPE))
                return false;

            if (rf.has_any(RequestQueryFlag::IGNORE_SELF_ACTOR) && selfActor && ctx.actor == selfActor)
                return false;

            if (rf.has_any(RequestQueryFlag::IGNORE_SAME_TEAM) && ctx.rqfd.id.Team() != 0 && ctx.qfd.id.Team() == ctx.rqfd.id.Team())
                return false;

            if (sqf.has_any(ShapeQueryFlag::PENETRABLE) && !rf.has_any(RequestQueryFlag::ACCEPT_PENETRABLE))
                return false;

            if (ctx.rqfd.meta.Sublayer() == QuerySublayer::LOS && sqf.has_any(ShapeQueryFlag::NO_LOS_BLOCK))
                return false;

            return true;
        }

        bool AcceptHit(const QueryEvaluateContext& /*ctx*/, const PxQueryHit& /*hit*/) const
        {
            return true;
        }
    };


    template<class PolicyT = DefaultQueryPolicy>
    struct QueryFilterCallbackT final : PxQueryFilterCallback
    {

        static_assert(
            std::is_invocable_r_v<bool, decltype(&PolicyT::AcceptCandidate), const PolicyT&, const QueryEvaluateContext&>,
            "PolicyT must implement: bool AcceptCandidate(const QueryEvaluateContext&) const");
        static_assert(
            std::is_invocable_r_v<bool, decltype(&PolicyT::AcceptHit), const PolicyT&, const QueryEvaluateContext&, const PxQueryHit&>,
            "PolicyT must implement: bool AcceptHit(const QueryEvaluateContext&, const PxQueryHit&) const");


        PolicyT             policy{};
        QueryHitTypeMap     hitTypeMap{};

        explicit QueryFilterCallbackT(PolicyT p = {}, QueryHitTypeMap m = {})
            : policy(std::move(p)), hitTypeMap(std::move(m))
        {
        }

        // preFilter: 구조적 gating + 정책 판단 → hit type 확정
        PxQueryHitType::Enum preFilter(const PxFilterData& fd, const PxShape* shape, const PxRigidActor* actor, PxHitFlags& /*hitFlags*/) override
        {
            const PxFilterData      rawSfd = shape->getQueryFilterData();

            // --- 프레임워크 구조적 gating ---
            if (!PassQueryCategory(rawSfd.word0, fd.word0))
                return PxQueryHitType::eNONE;

            const RequestQueryFD    req = RequestQueryFD::FromPx(fd);
            const QueryFD           shapeFD = QueryFD::FromPx(rawSfd);

            if (!PassChannelAndSublayer(shapeFD.meta, req.meta))
                return PxQueryHitType::eNONE;

            // --- 정책 의미적 판단 ---
            const QueryEvaluateContext ctx{ req, shapeFD, shape, actor };

            if (!policy.AcceptCandidate(ctx))
                return PxQueryHitType::eNONE;

            // --- HitType 결정 ---
            const eQueryHitMapMode mode = GetHitMapMode(req.flags);
            if (mode == eQueryHitMapMode::AllTouch) return PxQueryHitType::eTOUCH;
            if (mode == eQueryHitMapMode::AllBlock)  return PxQueryHitType::eBLOCK;

            return hitTypeMap.For(rawSfd.word0);
        }

        // postFilter: preFilter를 통과한 히트에 대해 AcceptHit 전용 판단
        //   - 구조적 gating은 이미 preFilter에서 통과됨
        //   - hit type은 preFilter와 동일 규칙으로 재결정 (shape는 동일 shape)
        PxQueryHitType::Enum postFilter(const PxFilterData& fd, const PxQueryHit& hit, const PxShape* shape, const PxRigidActor* actor) override
        {
            const PxFilterData      raw  = shape->getQueryFilterData();
            const RequestQueryFD    rqfd = RequestQueryFD::FromPx(fd);
            const QueryFD           qfd  = QueryFD::FromPx(raw);

            const QueryEvaluateContext ctx{ rqfd, qfd, shape, actor };

            if (!policy.AcceptHit(ctx, hit))
                return PxQueryHitType::eNONE;

            const eQueryHitMapMode mode = GetHitMapMode(rqfd.flags);
            if (mode == eQueryHitMapMode::AllTouch) return PxQueryHitType::eTOUCH;
            if (mode == eQueryHitMapMode::AllBlock)  return PxQueryHitType::eBLOCK;

            return hitTypeMap.For(raw.word0);
        }

    };

}