#pragma once

#include <jambase/EnumUtils.h>

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


    //struct SimCategory
    //{
	   // enum Enum : PxU32
	   // {
    //        NONE            = 0,
    //        WORLD_STATIC    = 1u << 0,
    //        WORLD_DYNAMIC   = 1u << 1,
    //        CHARACTER       = 1u << 2,
    //        PROJECTILE      = 1u << 3,
    //        SENSOR          = 1u << 4,
    //        ALL             = 0xFFFFFFFFu
	   // };

    //    using Flags = jam::FlagsT<Enum, PxU32>;
    //};

    struct QueryCategory
    {
        enum Enum : PxU32
        {
            NONE            = 0,
            WORLD           = 1u << 0,
            CHARACTER       = 1u << 1,
            HITBOX          = 1u << 2,
            TRIGGER         = 1u << 4,
            ALL             = 0xFFFFFFFFu
        };

        using Flags = jam::FlagsT<Enum, PxU32>;
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

    // ============================================================
    // 4) word1 officialization (QueryMeta / RequestMeta)
    // ============================================================
    // QueryMeta32 layout (shape.query.word1, request.word1 모두 동일 포맷 사용)
    //
    // bits  0..7  : channel/layer (0..255)   (예: 0=Default, 1=Gameplay, 2=Visibility...)
    // bits  8..15 : sublayer      (0..255)   (예: 0=Normal, 1=NoHitbox, ...)
    // bits 16..31 : tag16         (0..65535) (예: material tag, surface id, area id)
    //

    struct QueryMeta
    {
        PxU32 v{ 0 };

        static constexpr PxU32 Pack(PxU8 channel, PxU8 sublayer, PxU16 tag)
        {
            return (static_cast<PxU32>(channel)  & 0xFFu)
                | ((static_cast<PxU32>(sublayer) & 0xFFu) << 8)
                | ((static_cast<PxU32>(tag)    & 0xFFFFu) << 16);
        }

        static constexpr QueryMeta Make(PxU8 channel = 0, PxU8 sublayer = 0, PxU16 tag = 0)
        {
            return QueryMeta{ Pack(channel, sublayer, tag) };
        }

        constexpr PxU8  Channel()  const { return static_cast<PxU8>(v & 0xFFu); }
        constexpr PxU8  Sublayer() const { return static_cast<PxU8>((v >> 8) & 0xFFu); }
        constexpr PxU16 Tag()      const { return static_cast<PxU16>((v >> 16) & 0xFFFFu); }
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


            // HitTypeMapMode in bits [8..9]
            MAP_MODE_MASK       = 3u << 8,
            MAP_DEFAULT         = 0u << 8,
            MAP_ALL_TOUCH       = 1u << 8,
            MAP_ALL_BLOCK       = 2u << 8,
        };

        using Flags = jam::FlagsT<Enum, PxU32>;
    };

    enum class eQueryHitMapMode : PxU8
    {
	    Default,
        AllTouch,
        AllBlock,
    };


  //  struct SimUser
  //  {
  //      enum Enum : PxU32
  //      {
  //          NONE                = 0,

  //          // --- Touch notification (onContact or onTrigger) ---

  //          NOTIFY_TOUCH_FOUND          = 1u << 0,
  //          NOTIFY_TOUCH_LOST           = 1u << 1,
  //          NOTIFY_TOUCH_PERSISTS       = 1u << 2,      // not supported for trigger
  //          NOTIFY_TOUCH_CCD            = 1u << 3,      // requires CCD detect & non-trigger

  //          // --- Contact modification ---

  //          MODIFY_CONTACTS             = 1u << 4,      // PxPairFlag::eMODIFY_CONTACTS

  //          // --- Conatct points in reports (requires some notify/threshold flag to be meaningful) ---

  //          NOTIFY_CONTACT_POINTS       = 1u << 5,      // PxPairFlag::eNOTIFY_CONTACT_POINTS

  //          // --- Threshold force notification (contact only) ---

  //      	THRESHOLD_FORCE_FOUND       = 1u << 6,
  //          THRESHOLD_FORCE_PERSISTS    = 1u << 7,
  //          THRESHOLD_FORCE_LOST        = 1u << 8,

  //          PRE_SOLVER_VELOCITY         = 1u << 9,
  //          POST_SOLVER_VELOCITY        = 1u << 10,
  //          CONTACT_EVENT_POSE          = 1u << 11,

  //          // --- Policy toggles (optional) ---
		//	// If you want: "always request reports even if pairFlags doesn't include contact default"
		//	// Keep as reserved for future.

		//	RESERVED_12                 = 1u << 12,
		//	RESERVED_13                 = 1u << 13,
		//	RESERVED_14                 = 1u << 14,
		//	RESERVED_15                 = 1u << 15,
  //      };

  //      using Flags = jam::FlagsT<Enum, PxU32>;


  //      static constexpr PxU32 k_touchMask = 
  //          NOTIFY_TOUCH_FOUND | NOTIFY_TOUCH_LOST | NOTIFY_TOUCH_PERSISTS | NOTIFY_TOUCH_CCD;

  //      static constexpr PxU32 k_thresholdMask =
  //          THRESHOLD_FORCE_FOUND | THRESHOLD_FORCE_PERSISTS | THRESHOLD_FORCE_LOST;

  //      static constexpr PxU32 k_reportExtrasMask =
  //          PRE_SOLVER_VELOCITY | POST_SOLVER_VELOCITY | CONTACT_EVENT_POSE;

  //      static constexpr PxU32 k_needsDetectMask =
  //          k_touchMask | MODIFY_CONTACTS | NOTIFY_CONTACT_POINTS | k_thresholdMask | k_reportExtrasMask;

  //      static constexpr PxU32 k_contactOnlyMask =
  //          NOTIFY_TOUCH_PERSISTS | NOTIFY_TOUCH_CCD | MODIFY_CONTACTS | NOTIFY_CONTACT_POINTS | k_thresholdMask | k_reportExtrasMask;

  //      // Trigger pairs: 
  //      // persists not supported.
  //      // CCD touch notify not supported.
  //      // modify contacts not meaningful.
  //      // threshold/report extras not meaningful.
  //      static constexpr PxU32 k_triggerDisallowedMask =
  //          NOTIFY_TOUCH_PERSISTS | NOTIFY_TOUCH_CCD | MODIFY_CONTACTS | k_thresholdMask | k_reportExtrasMask;


  //      static constexpr PxU32 k_contactPointsNeedsSomethingMask =
  //          // If someone sets CONTACT_POINTS alone, we can either:
		//	// - auto add NOTIFY_TOUCH_FOUND (default choice), or
		//	// - drop CONTACT_POINTS.
		//	// We'll auto-add NOTIFY_TOUCH_FOUND by default (configurable).
  //          NOTIFY_CONTACT_POINTS;
  //  };


  //  enum class ePairKind : PxU8
  //  {
	 //   Auto,
  //      ForceContact,
  //      ForceTrigger,
  //  };

  //  enum class eCCDMode : PxU8
  //  {
	 //   Auto,               // prefer discrete unless SimUser asks for CCD touch notify
  //      PreferCCD,          // if possible, use CCD detect for contact pairs
  //      ForceDiscrete,      // never use CCD detect
  //      ForceCCD,           // use CCD detect (if impossible -> fallback : discrete)
  //  };



  //  struct PairFlagsBuildInput
  //  {
  //      PxFilterObjectAttributes    attrs0{};
  //      PxFilterData                fd0{};
  //      PxFilterObjectAttributes    attrs1{};
  //      PxFilterData                fd1{};

  //      ePairKind                   kind            = ePairKind::Auto;

  //      // set true only if scene + relevant bodies use CCD
  //      bool                        enableCCDDetect = false;       
  //      eCCDMode                    ccdMode         = eCCDMode::Auto;
  //  
		//// If CONTACT_POINTS is set alone, choose behavior:
  //      // - true  => auto add NOTIFY_TOUCH_FOUND
  //      // - false => drop CONTACT_POINTS
  //      bool                        autoAddNotifyForContactPoints = true;

  //      // If report extras(pre/post vel, pose) are set without any notify/threshold, choose behavior:
  //      // - true  => auto add NOTIFY_TOUCH_FOUND
  //      // - false => keep extras, but they not be delivered (usually pointless)
  //      bool                        autoAddNotifyForReportExtras  = true;
  //  };


  //  inline bool IsTriggerPair(PxFilterObjectAttributes attrs0 , PxFilterObjectAttributes attrs1)
  //  {
  //      return PxFilterObjectIsTrigger(attrs0) || PxFilterObjectIsTrigger(attrs1);
  //  }

  //  inline PxU32 ExtractSimUserBits(const PxFilterData& fd0, const PxFilterData& fd1) noexcept
  //  {
  //      return (fd0.word2 | fd1.word2);
  //  }


  //  struct PairFlagsBuilder
  //  {
  //      static PxPairFlags Build(const PairFlagsBuildInput& in)
  //      {
  //          const bool isTrigger =
  //              (in.kind == ePairKind::ForceTrigger) ? true  :
  //              (in.kind == ePairKind::ForceContact) ? false :
  //              IsTriggerPair(in.attrs0, in.attrs1);

  //          PxU32 u = ExtractSimUserBits(in.fd0, in.fd1);

  //          if (isTrigger)
  //          {
  //              u &= ~SimUser::k_triggerDisallowedMask;
  //          }

  //          PxPairFlags flags = isTrigger ? PxPairFlag::eTRIGGER_DEFAULT : PxPairFlag::eCONTACT_DEFAULT;

  //          if (u & SimUser::NOTIFY_TOUCH_FOUND) flags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
  //          if (u & SimUser::NOTIFY_TOUCH_LOST)  flags |= PxPairFlag::eNOTIFY_TOUCH_LOST;

  //          if (!isTrigger)
  //          {
  //              if (u & SimUser::NOTIFY_TOUCH_PERSISTS) flags |= PxPairFlag::eNOTIFY_TOUCH_PERSISTS;
  //              if (u & SimUser::NOTIFY_TOUCH_CCD)      flags |= PxPairFlag::eNOTIFY_TOUCH_CCD;
  //          }

  //          // Modify
  //          if (!isTrigger && (u & SimUser::MODIFY_CONTACTS))
  //              flags |= PxPairFlag::eMODIFY_CONTACTS;

  //          // Threshold force (contact only)
  //          if (!isTrigger)
  //          {
  //              if (u & SimUser::THRESHOLD_FORCE_FOUND)    flags |= PxPairFlag::eNOTIFY_THRESHOLD_FORCE_FOUND;
  //              if (u & SimUser::THRESHOLD_FORCE_PERSISTS) flags |= PxPairFlag::eNOTIFY_THRESHOLD_FORCE_PERSISTS;
  //              if (u & SimUser::THRESHOLD_FORCE_LOST)     flags |= PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST;
  //          }

  //          // Contact points (contact report extra)
  //          if (!isTrigger && (u & SimUser::NOTIFY_CONTACT_POINTS))
  //              flags |= PxPairFlag::eNOTIFY_CONTACT_POINTS;

  //          // Report stream extras (contact only)
  //          if (!isTrigger)
  //          {
  //              if (u & SimUser::PRE_SOLVER_VELOCITY)  flags |= PxPairFlag::ePRE_SOLVER_VELOCITY;
  //              if (u & SimUser::POST_SOLVER_VELOCITY) flags |= PxPairFlag::ePOST_SOLVER_VELOCITY;
  //              if (u & SimUser::CONTACT_EVENT_POSE)   flags |= PxPairFlag::eCONTACT_EVENT_POSE;
  //          }

  //          // Safety auto-add: if user asked for contact points or report extras without any trigger to emit reports
  //          // We treat "touch notify" or "threshold force notify" as "something that triggers a report".
  //          // If none present, CONTACT_POINTS / stream extras are typically useless.
  //          if (!isTrigger)
  //          {
  //              const bool hasSomeNotify =
  //                  (flags & (PxPairFlag::eNOTIFY_TOUCH_FOUND               |
  //                            PxPairFlag::eNOTIFY_TOUCH_LOST                |
  //                            PxPairFlag::eNOTIFY_TOUCH_PERSISTS            |
  //                            PxPairFlag::eNOTIFY_TOUCH_CCD                 |
  //                            PxPairFlag::eNOTIFY_THRESHOLD_FORCE_FOUND     |
  //                            PxPairFlag::eNOTIFY_THRESHOLD_FORCE_PERSISTS  |
  //                            PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST));

  //              const bool wantsContactPoints   = flags.isSet(PxPairFlag::eNOTIFY_CONTACT_POINTS);
  //              const bool wantsStreamExtras = (flags & (PxPairFlag::ePRE_SOLVER_VELOCITY | PxPairFlag::ePOST_SOLVER_VELOCITY | PxPairFlag::eCONTACT_EVENT_POSE));

  //              if (!hasSomeNotify)
  //              {
  //                  if (wantsContactPoints && in.autoAddNotifyForContactPoints)
  //                  {
  //                      flags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
  //                  }
  //                  else if (wantsContactPoints && !in.autoAddNotifyForContactPoints)
  //                  {
  //                      flags.clear(PxPairFlag::eNOTIFY_CONTACT_POINTS);
  //                  }

  //                  if (wantsStreamExtras && in.autoAddNotifyForReportExtras)
  //                  {
  //                      flags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
  //                  }
  //                  // else: keep extras, but they may not be delivered (rarely useful)
  //              }
  //          }

  //          // DETECT prerequisites:
  //          // If any notify/modify/threshold/stream requested, ensure detect discrete contact.
  //          const bool wantsDetect = 
  //              (u & SimUser::k_needsDetectMask) != 0 || (flags & (PxPairFlag::eNOTIFY_TOUCH_FOUND              |
  //                                                                 PxPairFlag::eNOTIFY_TOUCH_LOST               |
  //                                                                 PxPairFlag::eNOTIFY_TOUCH_PERSISTS           |
  //                                                                 PxPairFlag::eNOTIFY_TOUCH_CCD                |
  //                                                                 PxPairFlag::eMODIFY_CONTACTS                 |
  //                                                                 PxPairFlag::eNOTIFY_CONTACT_POINTS           |
  //                                                                 PxPairFlag::eNOTIFY_THRESHOLD_FORCE_FOUND    |
  //                                                                 PxPairFlag::eNOTIFY_THRESHOLD_FORCE_PERSISTS |
  //                                                                 PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST     |
  //                                                                 PxPairFlag::ePRE_SOLVER_VELOCITY             |
  //                                                                 PxPairFlag::ePOST_SOLVER_VELOCITY            |
  //                                                                 PxPairFlag::eCONTACT_EVENT_POSE));

  //          if (wantsDetect)
  //          {
  //              // Default: ensure discrete detect
  //              flags |= PxPairFlag::eDETECT_DISCRETE_CONTACT;
  //          }

  //          // CCD resolution:
  //          // If user asked for NOTIFY_TOUCH_CCD or engine policy wants CCD detect, try to enable CCD detect.
  //          // If impossible, fall back to discrete. Also remove CCD notify if CCD detect isn't enabled.
  //          if (!isTrigger)
  //          {
  //              const bool wantsCcdNotify = flags.isSet(PxPairFlag::eNOTIFY_TOUCH_CCD);
  //              const bool wantsCcdByMode = (in.ccdMode == eCCDMode::PreferCCD) || (in.ccdMode == eCCDMode::ForceCCD);

  //              bool enableCcdDetect = false;

  //              if (in.enableCCDDetect)
  //              {
  //                  if (in.ccdMode == eCCDMode::ForceDiscrete)
  //                  {
  //                      enableCcdDetect = false;
  //                  }
  //                  else if (in.ccdMode == eCCDMode::ForceCCD)
  //                  {
  //                      enableCcdDetect = true;
  //                  }
  //                  else if (wantsCcdNotify || wantsCcdByMode)
  //                  {
  //                      enableCcdDetect = true;
  //                  }
  //              }
  //              else
  //              {
  //                  // CCD not available in current configuration.
  //                  enableCcdDetect = false;
  //              }

  //              if (enableCcdDetect)
  //              {
  //                  flags |= PxPairFlag::eDETECT_CCD_CONTACT;
  //                  // For CCD notify to work, CCD detect must be present (we ensured it).
  //                  // Keep eNOTIFY_TOUCH_CCD if requested.
  //              }
  //              else
  //              {
  //                  // No CCD detect => CCD notify meaningless
  //                  flags.clear(PxPairFlag::eDETECT_CCD_CONTACT);
  //                  flags.clear(PxPairFlag::eNOTIFY_TOUCH_CCD);
  //              }
  //          }
  //          else
  //          {
  //              // Trigger pair: clear CCD detect/notify and other contact-only extras
  //              flags.clear(PxPairFlag::eDETECT_CCD_CONTACT);
  //              flags.clear(PxPairFlag::eNOTIFY_TOUCH_CCD);
  //              flags.clear(PxPairFlag::eNOTIFY_TOUCH_PERSISTS);
  //              flags.clear(PxPairFlag::eMODIFY_CONTACTS);
  //              flags.clear(PxPairFlag::eNOTIFY_THRESHOLD_FORCE_FOUND);
  //              flags.clear(PxPairFlag::eNOTIFY_THRESHOLD_FORCE_PERSISTS);
  //              flags.clear(PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST);
  //              flags.clear(PxPairFlag::ePRE_SOLVER_VELOCITY);
  //              flags.clear(PxPairFlag::ePOST_SOLVER_VELOCITY);
  //              flags.clear(PxPairFlag::eCONTACT_EVENT_POSE);
  //              flags.clear(PxPairFlag::eSOLVE_CONTACT); // triggers don't solve
  //          }

  //          // Final small sanity:
  //          // - If no notify/threshold/modify/points/extras requested, you can keep default as-is.
  //          // - DETECT_DISCRETE_CONTACT is harmless; leave it if we added it.
  //          // - You may optionally remove DETECT_DISCRETE_CONTACT for pure "solve only" pairs, but we don't.
  //          return flags;
  //      
  //      }
  //  };


  //  inline PxPairFlags BuildSafePairFlags(
  //      PxFilterObjectAttributes attrs0, const PxFilterData& fd0,
  //      PxFilterObjectAttributes attrs1, const PxFilterData& fd1,
  //      ePairKind kind = ePairKind::Auto)
  //  {
  //      PairFlagsBuildInput in{};
  //      in.attrs0 = attrs0; 
  //  	in.fd0    = fd0;
  //      in.attrs1 = attrs1; 
  //  	in.fd1    = fd1;
  //      in.kind   = kind;
  //      return PairFlagsBuilder::Build(in);
  //  }


    inline bool IsTriggerAttrs(PxFilterObjectAttributes attrs)
    {
        return PxFilterObjectIsTrigger(attrs);
    }


    constexpr eQueryHitMapMode GetHitMapMode(QueryRequest::Flags flags)
    {
        if (flags.bits() == QueryRequest::MAP_ALL_TOUCH) return eQueryHitMapMode::AllTouch;
        if (flags.bits() == QueryRequest::MAP_ALL_BLOCK) return eQueryHitMapMode::AllBlock;

        return eQueryHitMapMode::Default;
    }


    // ============================================================
    // 6) FilterData wrappers
    // ============================================================
 //   struct SimFD
 //   {
 //       SimCategory::Flags      category{};  // word0 
 //       SimCategory::Flags      mask{};      // word1 
 //       SimUser::Flags          userFlags{}; // word2 
 //       PxU32                   custom = 0;  // word3 

 //       static SimFD FromPx(const PxFilterData& fd)
 //       {
 //           SimFD sfd{};
 //           sfd.category    = static_cast<SimCategory::Flags>(fd.word0);
 //           sfd.mask        = static_cast<SimCategory::Flags>(fd.word1);
 //           sfd.userFlags   = static_cast<SimUser::Flags>(fd.word2);
 //           sfd.custom      = fd.word3;

 //           return sfd;
 //       }

 //       PxFilterData ToPx() const
 //       {
 //           PxFilterData fd;
 //           fd.word0 = category.bits();
 //           fd.word1 = mask.bits();
 //           fd.word2 = userFlags.bits();
 //           fd.word3 = custom;
 //           return fd;
 //       }
 //   };
 //   
	//inline void HashAppend(jam::Fnv1a32& h, const SimFD& fd) noexcept
 //   {
 //       HashAppend(h, fd.category.bits());
 //       HashAppend(h, fd.mask.bits());
 //       HashAppend(h, fd.userFlags.bits());
 //       HashAppend(h, fd.custom);
 //   }


    struct QueryFD
    {
        QueryCategory::Flags    category{}; // word0 
        QueryMeta               meta{};     // word1 
        PackedId32              id{};       // word2 
        ShapeQuery::Flags       flags{};    // word3 

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
        QueryCategory::Flags    mask{};           // word0
        QueryMeta               meta{};           // word1
        PackedId32              id{};             // word2
        QueryRequest::Flags     flags{};          // word3


        static QueryRequestFD FromPx(const PxFilterData& fd)
        {
            QueryRequestFD qrfd{};
            qrfd.mask   = static_cast<QueryCategory::Flags>(fd.word0);
            qrfd.meta.v = fd.word1;
            qrfd.id.v   = fd.word2;
            qrfd.flags  = static_cast<QueryRequest::Flags>(fd.word3);

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

    // ============================================================
    // 7) Common fast tests (LIB)
    // ============================================================


  

    constexpr bool PassQueryCategory(PxU32 shapeQueryCategoryBits, PxU32 requestedMaskBits)
    {
        return (shapeQueryCategoryBits & requestedMaskBits) != 0;
    }

    inline bool PassChannelAndSublayer(const QueryMeta& shapeMeta, const QueryMeta& reqMeta)
    {
        return shapeMeta.Channel() == reqMeta.Channel() && shapeMeta.Sublayer() == reqMeta.Sublayer();
    }

    // ============================================================
    // 8) Simulation FilterShader + Policy
    // ============================================================
    //struct DefaultSimPolicy
    //{
    //    static void ConfigurePairFlags(
    //        PxFilterObjectAttributes attrs0, const PxFilterData& fd0,
    //        PxFilterObjectAttributes attrs1, const PxFilterData& fd1,
    //        PxPairFlags& pairFlags)
    //    {
    //        PairFlagsBuildInput in{};
    //        in.attrs0 = attrs0; in.fd0 = fd0;
    //        in.attrs1 = attrs1; in.fd1 = fd1;

    //        pairFlags = PairFlagsBuilder::Build(in);
    //    }

    //    static PxFilterFlags PostDecision(
    //        PxFilterObjectAttributes, const PxFilterData&,
    //        PxFilterObjectAttributes, const PxFilterData&)
    //    {
    //        return PxFilterFlag::eDEFAULT;
    //    }
    //};

    //template<class PolicyT = DefaultSimPolicy>
    //PxFilterFlags SimulationFilterShader(
    //    PxFilterObjectAttributes attrs0, PxFilterData fd0,
    //    PxFilterObjectAttributes attrs1, PxFilterData fd1,
    //    PxPairFlags& pair,
    //    const void*, PxU32)
    //{
    //    if (!PassMask(fd0.word0, fd0.word1, fd1.word0, fd1.word1))
    //        return PxFilterFlag::eKILL;

    //    PolicyT::ConfigurePairFlags(attrs0, fd0, attrs1, fd1, pair);
    //    return PolicyT::PostDecision(attrs0, fd0, attrs1, fd1);
    //}



    // ============================================================
    //  Query HitType Mapping (category -> NONE/TOUCH/BLOCK)
    // ============================================================

    struct QueryHitTypeMap
    {
        PxQueryHitType::Enum world      = PxQueryHitType::eBLOCK;
        PxQueryHitType::Enum character  = PxQueryHitType::eBLOCK;
        PxQueryHitType::Enum hitbox     = PxQueryHitType::eBLOCK;
        PxQueryHitType::Enum trigger    = PxQueryHitType::eNONE;
        PxQueryHitType::Enum other      = PxQueryHitType::eBLOCK;

        PxQueryHitType::Enum For(const PxU32 category) const
        {
            if (category & QueryCategory::HITBOX)       return hitbox;
            if (category & QueryCategory::CHARACTER)    return character;
            if (category & QueryCategory::WORLD)        return world;
            if (category & QueryCategory::TRIGGER)      return trigger;
            return other;
        }
    };

    // ============================================================
    //  Query policy (accept/reject + postFilter distance cut)
    // ============================================================

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

            const QueryMeta reqMeta{ qfd.word1 };
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
        PolicyT             policy{};
        QueryHitTypeMap     map{};

        explicit QueryFilterCallbackT(PolicyT p = {}, QueryHitTypeMap m = {})
            : policy(std::move(p)), map(std::move(m))
        {
        }

        PxQueryHitType::Enum preFilter(const PxFilterData& qfd, const PxShape* shape, const PxRigidActor* actor, PxHitFlags& /*hitFlags*/) override
        {
            const PxFilterData sfd = shape->getQueryFilterData();

            if (!PassQueryCategory(sfd.word0, qfd.word0))
                return PxQueryHitType::eNONE;

            const QueryRequest::Flags rf{ static_cast<PxU32>(qfd.word3) };
            const QueryMeta shapeMeta{ sfd.word1 };
            const QueryMeta reqMeta{ qfd.word1 };

            if (!PassChannelAndSublayer(shapeMeta, reqMeta))
                return PxQueryHitType::eNONE;

            if (!policy.AcceptCandidate(qfd, shape, actor))
                return PxQueryHitType::eNONE;

            const eQueryHitMapMode mode = GetHitMapMode(rf);
            if (mode == eQueryHitMapMode::AllTouch) return PxQueryHitType::eTOUCH;
            if (mode == eQueryHitMapMode::AllBlock) return PxQueryHitType::eBLOCK;

            return map.For(sfd.word0);
        }

        PxQueryHitType::Enum postFilter(const PxFilterData& qfd, const PxQueryHit& hit, const PxShape* shape, const PxRigidActor* actor) override
        {
            if (!policy.AcceptHit(qfd, hit))
                return PxQueryHitType::eNONE;

            // preFilter와 동일 로직으로 hit type 결정 (recompute)
            const PxFilterData sfd = shape->getQueryFilterData();

            if (!PassQueryCategory(sfd.word0, qfd.word0))
                return PxQueryHitType::eNONE;

            const QueryRequest::Flags rf{ static_cast<PxU32>(qfd.word3) };
            const QueryMeta shapeMeta{ sfd.word1 };
            const QueryMeta reqMeta{ qfd.word1 };
            if (!PassChannelAndSublayer(shapeMeta, reqMeta))
                return PxQueryHitType::eNONE;

            if (!policy.AcceptCandidate(qfd, shape, actor))
                return PxQueryHitType::eNONE;

            const eQueryHitMapMode mode = GetHitMapMode(rf);
            if (mode == eQueryHitMapMode::AllTouch) return PxQueryHitType::eTOUCH;
            if (mode == eQueryHitMapMode::AllBlock) return PxQueryHitType::eBLOCK;

            return map.For(sfd.word0);
        }
    };



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

    //inline SimFD MakeSimFD(
    //    SimCategory::Flags  category, 
    //    SimCategory::Flags  mask, 
    //    SimUser::Flags      userFlags = SimUser::NONE, 
    //    PxU32               custom = 0)
    //{
    //    SimFD s{};
    //    s.category  = category;
    //    s.mask      = mask;
    //    s.userFlags = userFlags;
    //    s.custom    = custom;
    //    return s;
    //}

    inline QueryFD MakeShapeQueryFD(
        QueryCategory::Flags category, 
        PxU8  channel = 0, PxU8 sublayer = 0, PxU16 tag    = 0, 
        PxU16 teamId  = 0, PxU8 partId   = 0, PxU8  roldId = 0, 
        ShapeQuery::Flags flags = ShapeQuery::NONE)
    {
        QueryFD q{};
        q.category  = category;
        q.meta      = QueryMeta::Make(channel, sublayer, tag);
        q.id        = PackedId32::Make(teamId, partId, roldId);
        q.flags     = flags;
        return q;
    }

    inline QueryRequestFD MakeQueryRequestFD(
        QueryCategory::Flags mask, 
        PxU8  reqChannel = 0, PxU8 reqSubLayer = 0, PxU16 reqTag     = 0,
        PxU16 selfTeamId = 0, PxU8 selfPartId  = 0, PxU8  selfRoleId = 0,
        QueryRequest::Flags flags = QueryRequest::NONE)
    {
        QueryRequestFD r{};
        r.mask  = mask;
        r.meta  = QueryMeta::Make(reqChannel, reqSubLayer, reqTag);
        r.id    = PackedId32::Make(selfTeamId, selfPartId, selfRoleId);
        r.flags = flags;
        return r;
    }

} 

