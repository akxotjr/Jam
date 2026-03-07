#pragma once

#include "jampx/PhysicsSimFilter.h"
#include "jampx/PhysicsQueryFilter.h"

namespace jam::px
{

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


    inline QueryFD MakeShapeQueryFD(
        QueryCategory::Flags category, 
        PxU8  channel = 0, PxU8 sublayer = 0, PxU16 tag  = 0, 
        PxU16 team    = 0, PxU8 part     = 0, PxU8  role = 0, 
        ShapeQueryFlag::Flags flags = ShapeQueryFlag::NONE)
    {
        QueryFD q{};
        q.category  = category;
        q.meta      = QueryMeta::Make(channel, sublayer, tag);
        q.id        = PackedId32::Make(team, part, role);
        q.flags     = flags;
        return q;
    }

    inline RequestQueryFD MakeRequestQueryFD(
        QueryCategory::Flags mask, 
        PxU8  channel = 0, PxU8 sublayer = 0, PxU16 tag  = 0,
        PxU16 team    = 0, PxU8 part     = 0, PxU8  role = 0,
        RequestQueryFlag::Flags flags = RequestQueryFlag::NONE)
    {
        RequestQueryFD r{};
        r.mask  = mask;
        r.meta  = QueryMeta::Make(channel, sublayer, tag);
        r.id    = PackedId32::Make(team, part, role);
        r.flags = flags;
        return r;
    }

    inline RequestQueryFD MakeRequestQueryFD(
        PxU32 mask = 0,
        PxU8  channel = 0, PxU8 sublayer = 0, PxU16 tag = 0,
        PxU16 team = 0, PxU8 part = 0, PxU8  role = 0,
        PxU32 flags = 0)
    {
        RequestQueryFD r{};
        r.mask  = QueryCategory::Flags(mask);
        r.meta  = QueryMeta::Make(channel, sublayer, tag);
        r.id    = PackedId32::Make(team, part, role);
        r.flags = RequestQueryFlag::Flags(flags);
        return r;
    }


    static PxQueryFilterData MakePxQueryFilterData(
        const RequestQueryFD& rqfd, 
		const PxQueryFlags& flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER)
    {
        PxQueryFilterData out{};
        out.data  = rqfd.ToPx();
        out.flags = flags;

        return out;
    }
} 

