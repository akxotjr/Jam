// jam_physx_filter_final.h
// Single-header, C++20, PhysX 5.x
//
// ✅ FINAL Goals (찐 최종):
// 0) word0/1 = 라이브러리 고정 계약 / word2/3 = 커스텀 확장
// 1) Strong enum flags 유틸 (깔끔한 bit ops)
// 2) word2 패킹 규약 공식화: teamId(16) | partId(8) | user8(8)
// 3) Query TOUCH/BLOCK을 "카테고리별 매핑"으로 결정 (WORLD=BLOCK, HITBOX=TOUCH 등)
// 4) QueryFD.word1(예약영역)까지 공식화: Layer/Channel(8) + SubLayer(8) + Tag(16)
// 5) postFilter에서 distance 컷(옵션) + 추가 조건 확장 가능
//
// Design summary:
// - Simulation:
//   * fd.word0 = SimCategory (LIB)
//   * fd.word1 = SimMask     (LIB)
//   * fd.word2 = user flags  (USER; e.g. notify/modify)
//   * fd.word3 = user32      (USER)
//   * FilterShader: word0/1만으로 KILL/DEFAULT 결정, word2/3는 policy로 pairFlags 구성
//
// - Query (Shape):
//   * sfd.word0 = QueryCategory (LIB)
//   * sfd.word1 = QueryMeta (LIB-reserved encoding: layer/channel/sub/tag)
//   * sfd.word2 = PackedId32(teamId, partId, user8) (USER 규약)
//   * sfd.word3 = ShapeQF flags (USER)
//
// - Query (Request / qfd):
//   * qfd.word0 = desiredMask (LIB)
//   * qfd.word1 = RequestMeta (LIB-reserved encoding: queryChannel etc.)
//   * qfd.word2 = PackedId32(selfTeamId, selfPartId, selfUser8) (USER 규약)
//   * qfd.word3 = QueryRF flags + HitTypeMapMode + distance cut options (USER)
//
// - Query decision pipeline:
//   1) LIB: category gating (sfd.word0 & qfd.word0)
//   2) LIB: layer/channel gating (sfd.word1 vs qfd.word1)
//   3) USER policy: accept/reject (self/team/trigger/penetrable/los 등)
//   4) LIB: category->hitType mapping (TOUCH/BLOCK/NONE) 결정
//   5) USER policy: postFilter (distance cut 등) 추가 컷
//
// Drop-in use: include and set sceneDesc.filterShader, set shape filters, and use QueryFilterCallbackT.

#pragma once

#include <jambase/EnumUtils.h>

#include <cstdint>
#include <type_traits>

#include "jambase/Fnv1a.h"

namespace jam::px
{
    using namespace ::physx;


    enum class eShapeFlag : uint8_t
    {
        SIMULATION,
        SIMULATION_ONLY,
        TRIGGER,
        TRIGGER_ONLY,
        QUERY_ONLY
    };

    inline void SetupShapeFlags(PxShape& shape, eShapeFlag flag)
    {
        switch (flag)
        {
        case eShapeFlag::SIMULATION:
            shape.setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
            shape.setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
            shape.setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
            return;

        case eShapeFlag::SIMULATION_ONLY:
            shape.setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
            shape.setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
            shape.setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
            return;

        case eShapeFlag::TRIGGER:
            shape.setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
            shape.setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
            shape.setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
            return;

        case eShapeFlag::TRIGGER_ONLY:
            shape.setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
            shape.setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
            shape.setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
            return;

        case eShapeFlag::QUERY_ONLY:
            shape.setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
            shape.setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
            shape.setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
            return;
        }
    }


    struct SimCategory
    {
	    enum Enum : PxU32
	    {
            NONE            = 0,
            WORLD_STATIC    = 1u << 0,
            WORLD_DYNAMIC   = 1u << 1,
            CHARACTER       = 1u << 2,
            PROJECTILE      = 1u << 3,
            TRIGGER         = 1u << 4,
            SENSOR          = 1u << 5,
            ALL             = 0xFFFFFFFFu
	    };

        using Flags = jam::FlagsT<Enum, PxU32>;
    };

    struct QueryCategory
    {
        enum Enum : PxU32
        {
            NONE        = 0,
            WORLD       = 1u << 0,
            CHARACTER   = 1u << 1,
            HITBOX      = 1u << 2,
            TRIGGER     = 1u << 4,
            ALL         = 0xFFFFFFFFu
        };

        using Flags = jam::FlagsT<Enum, PxU32>;
    };

    // ============================================================
    // 3) word2 packing (team16 | part8 | user8)
    // ============================================================
    struct PackedId32
    {
        PxU32 v{ 0 };

        static constexpr PxU32 Pack(PxU16 teamId, PxU8 partId, PxU8 user8)
        {
            return (static_cast<PxU32>(teamId) & 0xFFFFu)
                | ((static_cast<PxU32>(partId) & 0xFFu) << 16)
                | ((static_cast<PxU32>(user8)  & 0xFFu) << 24);
        }

        static constexpr PackedId32 Make(PxU16 teamId, PxU8 partId = 0, PxU8 user8 = 0)
        {
            return PackedId32{ Pack(teamId, partId, user8) };
        }

        constexpr PxU16 Team()  const { return static_cast<PxU16>(v & 0xFFFFu); }
        constexpr PxU8  Part()  const { return static_cast<PxU8>((v >> 16) & 0xFFu); }
        constexpr PxU8  User8() const { return static_cast<PxU8>((v >> 24) & 0xFFu); }
    };

    // ============================================================
    // 4) word1 officialization (QueryMeta / RequestMeta)
    // ============================================================
    // QueryMeta32 layout (shape.query.word1, request.word1 모두 동일 포맷 사용)
    //
    // bits  0..7  : channel/layer (0..255)   (예: 0=Default, 1=Gameplay, 2=Visibility...)
    // bits  8..15 : sublayer      (0..255)   (예: 0=Normal, 1=NoHitbox, ...)
    // bits 16..31 : tag16         (0..65535) (예: material tag, surface id, area id)
    //
    // "일단 같은 channel만 서로 통과" 같은 룰을 만들기 쉬움.
    struct QueryMeta32
    {
        PxU32 v{ 0 };

        static constexpr PxU32 Pack(PxU8 channel, PxU8 sublayer, PxU16 tag16)
        {
            return (static_cast<PxU32>(channel)  & 0xFFu)
                | ((static_cast<PxU32>(sublayer) & 0xFFu) << 8)
                | ((static_cast<PxU32>(tag16)    & 0xFFFFu) << 16);
        }

        static constexpr QueryMeta32 Make(PxU8 channel = 0, PxU8 sublayer = 0, PxU16 tag16 = 0)
        {
            return QueryMeta32{ Pack(channel, sublayer, tag16) };
        }

        constexpr PxU8  Channel()  const { return static_cast<PxU8>(v & 0xFFu); }
        constexpr PxU8  Sublayer() const { return static_cast<PxU8>((v >> 8) & 0xFFu); }
        constexpr PxU16 Tag16()    const { return static_cast<PxU16>((v >> 16) & 0xFFFFu); }
    };

    // Layer matching policy for LIB
    enum class LayerMatchMode : PxU32
    {
        // request.channel == shape.channel 만 허용 (가장 흔함)
        SameChannel = 0,

        // request.channel bitmask로 해석(고급): shape.channel을 bit index로 보고 포함되면 통과
        ChannelMask = 1,
    };

    // ============================================================
    // 5) USER flags (word3)
    // ============================================================
    struct ShapeQuery
    {
        enum Enum : PxU32
        {
            NONE            = 0,
            IS_HEAD         = 1u << 0,
            PENETRABLE      = 1u << 1,
            NO_LOS_BLOCK    = 1u << 2,
        };

        using Flags = jam::FlagsT<Enum, PxU32>;
    };

    // QueryRF: request flags + hitTypeMapMode
    struct QueryRequest
    {
        enum Enum : PxU32
        {
            NONE                = 0,
            IGNORE_TRIGGERS     = 1u << 0,
            IGNORE_SELF_ACTOR   = 1u << 1,
            IGNORE_SAME_TEAM    = 1u << 2,
            ACCEPT_PENETRABLE   = 1u << 3,

            // Layer match mode in bits [6..7] (2-bit)
            LAYER_MODE_MASK     = 3u << 6,
            LAYER_SAME_CHANNEL  = 0u << 6,
            LAYER_CHANNEL_MASK  = 1u << 6,

            // HitTypeMapMode in bits [8..9]
            MAP_MODE_MASK       = 3u << 8,
            MAP_DEFAULT         = 0u << 8,
            MAP_ALL_TOUCH       = 1u << 8,
            MAP_ALL_BLOCK       = 2u << 8,
        };

        using Flags = jam::FlagsT<Enum, PxU32>;
    };

    // Simulation user flags example (stored in sim.word2)
    struct SimUser
    {
        enum Enum : PxU32
        {
            NONE = 0,
            NOTIFY_TOUCH    = 1u << 0,
            NOTIFY_LOST     = 1u << 1,
            NOTIFY_CONTACTS = 1u << 2,
        };

        using Flags = jam::FlagsT<Enum, PxU32>;
    };


    constexpr LayerMatchMode GetLayerMode(QueryRequest::Flags rf)
    {
        const PxU32 m = rf.bits() & QueryRequest::Flags(QueryRequest::LAYER_MODE_MASK).bits();
        if (m == QueryRequest::Flags(QueryRequest::LAYER_CHANNEL_MASK).bits()) 
            return LayerMatchMode::ChannelMask;
        return LayerMatchMode::SameChannel;
    }

    constexpr QueryRequest::Flags GetMapMode(QueryRequest::Flags rf)
    {
        return QueryRequest::Flags(rf.bits() & QueryRequest::Flags(QueryRequest::MAP_MODE_MASK).bits());
    }



    // ============================================================
    // 6) FilterData wrappers
    // ============================================================
    struct SimFD
    {
        SimCategory::Flags      category{}; // word0 (LIB)
        SimCategory::Flags      mask{};     // word1 (LIB)
        SimUser::Flags          userFlags{}; // word2 (USER)
        PxU32                   user3{ 0 };  // word3 (USER)

        static SimFD FromPx(const PxFilterData& fd)
        {
            SimFD sfd{};
            sfd.category    = static_cast<SimCategory::Flags>(fd.word0);
            sfd.mask        = static_cast<SimCategory::Flags>(fd.word1);
            sfd.userFlags   = static_cast<SimUser::Flags>(fd.word2);
            sfd.user3       = fd.word3;

            return sfd;
        }

        PxFilterData ToPx() const
        {
            PxFilterData fd;
            fd.word0 = category.bits();
            fd.word1 = mask.bits();
            fd.word2 = userFlags.bits();
            fd.word3 = user3;
            return fd;
        }
    };
    
	inline void HashAppend(jam::Fnv1a32& h, const SimFD& fd) noexcept
    {
        HashAppend(h, fd.category.bits());
        HashAppend(h, fd.mask.bits());
        HashAppend(h, fd.userFlags.bits());
        HashAppend(h, fd.user3);
    }


    struct QueryFD
    {
        QueryCategory::Flags category{}; // word0 (LIB)
        QueryMeta32          meta{};     // word1 (LIB reserved)
        PackedId32           id{};       // word2 (USER packed)
        ShapeQuery::Flags    flags{};    // word3 (USER)

        static QueryFD FromPx(const PxFilterData& fd)
        {
            QueryFD qfd{};
            qfd.category = static_cast<QueryCategory::Flags>(fd.word0);
            qfd.meta.v   = fd.word1;
            qfd.id.v     = fd.word2;
            qfd.flags    = static_cast<ShapeQuery::Flags>(fd.word3);
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

    struct QueryRequestFD
    {
        QueryCategory::Flags    desiredMask{}; // word0 (LIB)
        QueryMeta32             meta{};        // word1 (LIB reserved)
        PackedId32              self{};        // word2 (USER packed)
        QueryRequest::Flags     rf{};          // word3 (USER)


        static QueryRequestFD FromPx(const PxFilterData& fd)
        {
            QueryRequestFD qrfd{};
            qrfd.desiredMask = static_cast<QueryCategory::Flags>(fd.word0);
            qrfd.meta.v      = fd.word1;
            qrfd.self.v      = fd.word2;
            qrfd.rf          = static_cast<QueryRequest::Flags>(fd.word3);

            return qrfd;
        }

        PxFilterData ToPx() const
        {
            PxFilterData fd;
            fd.word0 = desiredMask.bits();
            fd.word1 = meta.v;
            fd.word2 = self.v;
            fd.word3 = rf.bits();
            return fd;
        }
    };

    // ============================================================
    // 7) Common fast tests (LIB)
    // ============================================================
    inline bool IsTriggerAttrs(PxFilterObjectAttributes attrs)
    {
        return PxFilterObjectIsTrigger(attrs);
    }

    constexpr bool PassMask(PxU32 aCategory, PxU32 aMask, PxU32 bCategory, PxU32 bMask)
    {
        if ((aCategory & bMask) == 0) return false;
        if ((bCategory & aMask) == 0) return false;
        return true;
    }

    constexpr bool PassQueryCategory(PxU32 shapeQueryCategoryBits, PxU32 requestedMaskBits)
    {
        return (shapeQueryCategoryBits & requestedMaskBits) != 0;
    }

    inline bool PassLayerChannel(const QueryMeta32& shapeMeta, const QueryMeta32& reqMeta, LayerMatchMode mode)
    {
        if (mode == LayerMatchMode::SameChannel)
        {
            // request channel == shape channel
            return shapeMeta.Channel() == reqMeta.Channel();
        }
        else
        {
            // request channel is a bitmask (8-bit), shape.channel is index [0..7]
            const PxU8 mask = reqMeta.Channel();
            const PxU8 idx = shapeMeta.Channel() & 7;
            return (mask & (static_cast<PxU8>(1u) << idx)) != 0;
        }
    }

    // ============================================================
    // 8) Simulation FilterShader + Policy
    // ============================================================
    struct DefaultSimPolicy
    {
        static void ConfigurePairFlags(
            PxFilterObjectAttributes, const PxFilterData&,
            PxFilterObjectAttributes, const PxFilterData&,
            PxPairFlags&)
        {
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
        if (!PassMask(fd0.word0, fd0.word1, fd1.word0, fd1.word1))
            return PxFilterFlag::eKILL;

        const bool isTrigger =
            IsTriggerAttrs(attrs0) || IsTriggerAttrs(attrs1) ||
            ((fd0.word0 & SimCategory::Flags(SimCategory::TRIGGER).bits()) != 0) ||
            ((fd1.word0 & SimCategory::Flags(SimCategory::TRIGGER).bits()) != 0);

        if (isTrigger)
        {
            pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
            PolicyT::ConfigurePairFlags(attrs0, fd0, attrs1, fd1, pairFlags);
            return PolicyT::PostDecision(attrs0, fd0, attrs1, fd1);
        }

        pairFlags = PxPairFlag::eCONTACT_DEFAULT;
        PolicyT::ConfigurePairFlags(attrs0, fd0, attrs1, fd1, pairFlags);
        return PolicyT::PostDecision(attrs0, fd0, attrs1, fd1);
    }

    struct ExampleSimPolicy
    {
        static void ConfigurePairFlags(
            PxFilterObjectAttributes, const PxFilterData& fd0,
            PxFilterObjectAttributes, const PxFilterData& fd1,
            PxPairFlags& pairFlags)
        {
            const SimUser::Flags uf(static_cast<PxU32>(fd0.word2 | fd1.word2));
            if (uf.has_any(SimUser::Flags(SimUser::NOTIFY_TOUCH)))    pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
            if (uf.has_any(SimUser::Flags(SimUser::NOTIFY_LOST)))     pairFlags |= PxPairFlag::eNOTIFY_TOUCH_LOST;
            if (uf.has_any(SimUser::Flags(SimUser::NOTIFY_CONTACTS))) pairFlags |= PxPairFlag::eMODIFY_CONTACTS;
        }

        static PxFilterFlags PostDecision(
            PxFilterObjectAttributes, const PxFilterData&,
            PxFilterObjectAttributes, const PxFilterData&)
        {
            return PxFilterFlag::eDEFAULT;
        }
    };

    // ============================================================
    // 9) Query HitType Mapping (category -> NONE/TOUCH/BLOCK)
    // ============================================================
    // 원하는 카테고리별로 hit type을 매핑해서,
    // - 예: WORLD=BLOCK, HITBOX=TOUCH, CHARACTER=TOUCH, TRIGGER=NONE...
    //
    // Map is stored in callback instance (per query), not in global static.
    // That lets you do:
    // - Aim ray: WORLD=BLOCK, HITBOX=BLOCK, CHARACTER=BLOCK (closest block)
    // - Near overlap: WORLD=NONE, CHARACTER=TOUCH, HITBOX=TOUCH (collect)
    // - Special: WORLD=BLOCK, HITBOX=TOUCH (hitbox list while world blocks LOS)

    struct QueryHitTypeMap
    {
        PxQueryHitType::Enum world      = PxQueryHitType::eBLOCK;
        PxQueryHitType::Enum character  = PxQueryHitType::eBLOCK;
        PxQueryHitType::Enum hitbox     = PxQueryHitType::eBLOCK;
        PxQueryHitType::Enum trigger    = PxQueryHitType::eNONE;

        PxQueryHitType::Enum For(const PxFilterData& shapeQfd) const
        {
            const PxU32 c = shapeQfd.word0;
            if (c & QueryCategory::Flags(QueryCategory::HITBOX).bits())     return hitbox;
            if (c & QueryCategory::Flags(QueryCategory::CHARACTER).bits())  return character;
            if (c & QueryCategory::Flags(QueryCategory::WORLD).bits())      return world;
            if (c & QueryCategory::Flags(QueryCategory::TRIGGER).bits())    return trigger;
            return PxQueryHitType::eBLOCK;
        }

        static QueryHitTypeMap AimClosestBlock()
        {
            QueryHitTypeMap m;
            m.world     = m.character = m.hitbox = PxQueryHitType::eBLOCK;
            m.trigger   = PxQueryHitType::eNONE;
            return m;
        }

        static QueryHitTypeMap CollectNearby()
        {
            QueryHitTypeMap m;
            m.world     = PxQueryHitType::eNONE;
            m.character = PxQueryHitType::eTOUCH;
            m.hitbox    = PxQueryHitType::eTOUCH;
            m.trigger   = PxQueryHitType::eNONE;
            return m;
        }

        static QueryHitTypeMap HitboxTouchWorldBlock()
        {
            QueryHitTypeMap m;
            m.world     = PxQueryHitType::eBLOCK;
            m.hitbox    = PxQueryHitType::eTOUCH;
            m.character = PxQueryHitType::eNONE;
            m.trigger   = PxQueryHitType::eNONE;
            return m;
        }
    };

    // ============================================================
    // 10) Query policy (accept/reject + postFilter distance cut)
    // ============================================================
    // Default: accept everything, no distance cut.
    struct DefaultQueryPolicy
    {
        const PxRigidActor* selfActor = nullptr;

        bool AcceptCandidate(const PxFilterData& /*qfd*/, const PxShape* /*shape*/, const PxRigidActor* /*actor*/) const
        {
            return true;
        }

        bool AcceptHit(const PxFilterData& /*qfd*/, const PxQueryHit& /*hit*/) const
        {
            return true;
        }
    };

    // Example: self/team/trigger/penetrable + LOS ignore + distance cut
    struct ExampleQueryPolicy
    {
        const PxRigidActor* selfActor = nullptr;

        bool AcceptCandidate(const PxFilterData& qfd, const PxShape* shape, const PxRigidActor* actor) const
        {
            const PxFilterData sfd = shape->getQueryFilterData();
            const QueryRequest::Flags rf{ static_cast<PxU32>(qfd.word3) };

            const PackedId32 self{ qfd.word2 };
            const PackedId32 other{ sfd.word2 };
            const ShapeQuery::Flags sqf{ static_cast<PxU32>(sfd.word3) };

            if (rf.has_any(QueryRequest::IGNORE_TRIGGERS) && (shape->getFlags() & PxShapeFlag::eTRIGGER_SHAPE))
                return false;

            if (rf.has_any(QueryRequest::IGNORE_SELF_ACTOR) && selfActor && actor == selfActor)
                return false;

            if (rf.has_any(QueryRequest::IGNORE_SAME_TEAM) && self.Team() != 0 && other.Team() == self.Team())
                return false;

            if (sqf.has_any(ShapeQuery::PENETRABLE) && !rf.has_any(QueryRequest::ACCEPT_PENETRABLE))
                return false;

            const QueryMeta32 reqMeta{ qfd.word1 };
            if (reqMeta.Sublayer() == 1 /*LOS*/ && sqf.has_any(ShapeQuery::NO_LOS_BLOCK))
                return false;

            return true;
        }

        bool AcceptHit(const PxFilterData&, const PxQueryHit&) const
        {
            return true;
        }
    };

    // ============================================================
    // 11) QueryFilterCallback (최종) : gating + policy + mapping + postFilter
    // ============================================================
    template<class PolicyT = DefaultQueryPolicy>
    struct QueryFilterCallbackT final : PxQueryFilterCallback
    {
        PolicyT policy{};
        QueryHitTypeMap map{};

        explicit QueryFilterCallbackT(PolicyT p = {}, QueryHitTypeMap m = {})
            : policy(std::move(p)), map(std::move(m))
        {
        }

        PxQueryHitType::Enum preFilter(
            const PxFilterData& qfd,
            const PxShape* shape,
            const PxRigidActor* actor,
            PxHitFlags& /*hitFlags*/) override
        {
            const PxFilterData sfd = shape->getQueryFilterData();

            if (!PassQueryCategory(sfd.word0, qfd.word0))
                return PxQueryHitType::eNONE;

            const QueryRequest::Flags rf{ static_cast<PxU32>(qfd.word3) };
            const LayerMatchMode lm = GetLayerMode(rf);
            const QueryMeta32 shapeMeta{ sfd.word1 };
            const QueryMeta32 reqMeta{ qfd.word1 };
            if (!PassLayerChannel(shapeMeta, reqMeta, lm))
                return PxQueryHitType::eNONE;

            if (!policy.AcceptCandidate(qfd, shape, actor))
                return PxQueryHitType::eNONE;

            const QueryRequest::Flags mode = GetMapMode(rf);
            if (mode.bits() == QueryRequest::Flags(QueryRequest::MAP_ALL_TOUCH).bits()) return PxQueryHitType::eTOUCH;
            if (mode.bits() == QueryRequest::Flags(QueryRequest::MAP_ALL_BLOCK).bits()) return PxQueryHitType::eBLOCK;

            return map.For(sfd);
        }

        PxQueryHitType::Enum postFilter(const PxFilterData& qfd, const PxQueryHit& hit, const PxShape*, const PxRigidActor*) override
        {
            if (!policy.AcceptHit(qfd, hit))
                return PxQueryHitType::eNONE;

            return PxQueryHitType::eBLOCK;
        }
    };

    // ============================================================
    // 12) Convenience API: shape filter setup + builders
    // ============================================================
    inline void SetShapeSimFilter(PxShape& shape, const SimFD& sim)
    {
        shape.setSimulationFilterData(sim.ToPx());
    }

    inline void SetShapeQueryFilter(PxShape& shape, const QueryFD& qry)
    {
        shape.setQueryFilterData(qry.ToPx());
    }

    inline void SetShapeFilters(PxShape& shape, const SimFD& sim, const QueryFD& qry)
    {
        shape.setSimulationFilterData(sim.ToPx());
        shape.setQueryFilterData(qry.ToPx());
    }

    inline void ApplyShapeFilters(PxShape& shape, eShapeFlag flags, const SimFD& sim, const QueryFD& qry)
    {
        SetupShapeFlags(shape, flags);
        SetShapeFilters(shape, sim, qry);
    }

    inline SimFD MakeSimFD(SimCategory::Flags category, SimCategory::Flags mask, SimUser::Flags userFlags = {}, PxU32 user3 = 0)
    {
        SimFD s{};
        s.category  = category;
        s.mask      = mask;
        s.userFlags = userFlags;
        s.user3     = user3;
        return s;
    }

    inline QueryFD MakeShapeQueryFD(QueryCategory::Flags category, PxU8 channel, PxU8 sublayer, PxU16 tag16, PxU16 teamId, PxU8 partId, ShapeQuery::Flags flags = {}, PxU8 user8 = 0)
    {
        QueryFD q{};
        q.category  = category;
        q.meta      = QueryMeta32::Make(channel, sublayer, tag16);
        q.id        = PackedId32::Make(teamId, partId, user8);
        q.flags     = flags;
        return q;
    }

    inline QueryRequestFD MakeQueryRequestFD(
        QueryCategory::Flags desiredMask,
        PxU8 reqChannel, PxU8 reqSubLayer, PxU16 reqTag16,
        PxU16 selfTeamId,
        QueryRequest::Flags rf,
        PxU8 selfPartId = 0,
        PxU8 selfUser8 = 0)
    {
        QueryRequestFD r{};
        r.desiredMask = desiredMask;
        r.meta        = QueryMeta32::Make(reqChannel, reqSubLayer, reqTag16);
        r.self        = PackedId32::Make(selfTeamId, selfPartId, selfUser8);
        r.rf          = rf;
        return r;
    }

    // ============================================================
    // 13) Usage cookbook (copy-paste)
    // ============================================================
    //
    // --- Scene setup (Simulation) ---
    // PxSceneDesc desc(physics->getTolerancesScale());
    // desc.filterShader = &jam::physx_filter::SimulationFilterShader<jam::physx_filter::ExampleSimPolicy>;
    //
    // --- Shape setup (shared shape: 초기 1회만!) ---
    // auto sim = MakeSimFD(SimCategory::CHARACTER, SimCategory::ALL, SimUF::NOTIFY_TOUCH);
    // auto qry = MakeShapeQueryFD(
    //     QueryCategory::CHARACTER,
    //     /*channel*/0, /*sublayer*/0, /*tag*/0,
    //     /*team*/1, /*part*/0,
    //     ShapeQF::NONE);
    // SetShapeFilters(*shape, sim, qry);
    //
    // --- Aim ray (closest BLOCK) ---
    // // request: channel 0 only, sublayer 0 normal, tag16=0
    // auto req = MakeQueryRequestFD(
    //     QueryCategory::WORLD | QueryCategory::CHARACTER | QueryCategory::HITBOX,
    //     /*reqChannel*/0, /*reqSub*/0, /*reqTag*/0,
    //     /*selfTeam*/1,
    //     QueryRF::IGNORE_SELF_ACTOR | QueryRF::IGNORE_SAME_TEAM | QueryRF::IGNORE_TRIGGERS | QueryRF::MAP_DEFAULT);
    //
    // PxQueryFilterData qfd(req.ToPx(), PxQueryFlag::ePREFILTER | PxQueryFlag::ePOSTFILTER);
    // ExampleQueryPolicy pol{ .selfActor = myActor };
    // auto map = QueryHitTypeMap::AimClosestBlock();
    // QueryFilterCallbackT<ExampleQueryPolicy> cb(pol, map);
    // scene->raycast(origin, dir, dist, hit, hitFlags, qfd, &cb);
    //
    // --- Nearby overlap (collect) ---
    // auto req2 = MakeQueryRequestFD(
    //     QueryCategory::CHARACTER | QueryCategory::HITBOX,
    //     0, 0, 0,
    //     1,
    //     QueryRF::IGNORE_SELF_ACTOR | QueryRF::IGNORE_SAME_TEAM | QueryRF::IGNORE_TRIGGERS | QueryRF::MAP_ALL_TOUCH);
    // PxQueryFilterData qfd2(req2.ToPx(), PxQueryFlag::ePREFILTER);
    // QueryFilterCallbackT<ExampleQueryPolicy> cb2(pol, QueryHitTypeMap::CollectNearby());
    // scene->overlap(geom, pose, buf, qfd2, &cb2);
    //
    // --- HITBOX TOUCH + WORLD BLOCK (LOS+hitbox list) ---
    // auto req3 = MakeQueryRequestFD(
    //     QueryCategory::WORLD | QueryCategory::HITBOX,
    //     0, 0, 0,
    //     1,
    //     QueryRF::IGNORE_SELF_ACTOR | QueryRF::IGNORE_SAME_TEAM | QueryRF::IGNORE_TRIGGERS | QueryRF::MAP_DEFAULT);
    // auto map3 = QueryHitTypeMap::HitboxTouchWorldBlock();
    // QueryFilterCallbackT<ExampleQueryPolicy> cb3(pol, map3);
    //
    // --- Distance cut (ex: 2500cm = 25m) ---
    // auto req4 = WithDistanceCutCm(req, /*maxDistCm*/2500);
    // PxQueryFilterData qfd4(req4.ToPx(), PxQueryFlag::ePREFILTER | PxQueryFlag::ePOSTFILTER);
    // QueryFilterCallbackT<ExampleQueryPolicy> cb4(pol, map);
    //
} 

