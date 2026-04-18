#pragma once

#include "jamnet/core/net/RPC.h"
#include "jamnet/runtime/schema/gen/binding_handshake_generated.h"
#include "jamnet/runtime/schema/gen/world_assignment_generated.h"
#include "jamnet/sync/schema/gen/actor_control_generated.h"
#include "jamnet/sync/schema/gen/actor_spawn_generated.h"

namespace jam::net
{
    namespace rpc_id
    {
        template<size_t N>
        constexpr bool AreUnique(const uint16 (&ids)[N])
        {
            for (size_t i = 0; i < N; ++i)
            {
                for (size_t j = i + 1; j < N; ++j)
                {
                    if (ids[i] == ids[j])
                        return false;
                }
            }
            return true;
        }

        constexpr uint16 TCP_BIND_REQ                  = 0x0101;
        constexpr uint16 TCP_BIND_RES                  = 0x0102;
        constexpr uint16 UDP_BIND_REQ                  = 0x0103;
        constexpr uint16 UDP_BIND_RES                  = 0x0104;

        constexpr uint16 REQUEST_WORLD_ASSIGNMENT_REQ  = 0x0111;
        constexpr uint16 REQUEST_WORLD_ASSIGNMENT_RES  = 0x0112;

        constexpr uint16 SPAWN_ACTOR_REQ               = 0x0201;
        constexpr uint16 SPAWN_ACTOR_RES               = 0x0202;
        constexpr uint16 DESPAWN_ACTOR_REQ             = 0x0203;
        constexpr uint16 DESPAWN_ACTOR_RES             = 0x0204;

        constexpr uint16 POSSESS_ACTOR_REQ             = 0x0211;
        constexpr uint16 POSSESS_ACTOR_RES             = 0x0212;
        constexpr uint16 UNPOSSESS_ACTOR_REQ           = 0x0213;
        constexpr uint16 UNPOSSESS_ACTOR_RES           = 0x0214;

        constexpr uint16 ALL[] = {
            TCP_BIND_REQ, TCP_BIND_RES, UDP_BIND_REQ, UDP_BIND_RES,
            REQUEST_WORLD_ASSIGNMENT_REQ, REQUEST_WORLD_ASSIGNMENT_RES,
            SPAWN_ACTOR_REQ, SPAWN_ACTOR_RES, DESPAWN_ACTOR_REQ, DESPAWN_ACTOR_RES,
            POSSESS_ACTOR_REQ, POSSESS_ACTOR_RES, UNPOSSESS_ACTOR_REQ, UNPOSSESS_ACTOR_RES
        };

        static_assert(AreUnique(ALL), "RPC ids must be unique.");
    }

    template<> struct RPCIdTraits<fb::fbTcpBindReq> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::TCP_BIND_REQ; };
    template<> struct RPCIdTraits<fb::fbTcpBindRes> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::TCP_BIND_RES; };
    template<> struct RPCIdTraits<fb::fbUdpBindReq> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::UDP_BIND_REQ; };
    template<> struct RPCIdTraits<fb::fbUdpBindRes> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::UDP_BIND_RES; };

    template<> struct RPCIdTraits<fb::fbRequestWorldAssignmentReq> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::REQUEST_WORLD_ASSIGNMENT_REQ; };
    template<> struct RPCIdTraits<fb::fbRequestWorldAssignmentRes> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::REQUEST_WORLD_ASSIGNMENT_RES; };

    template<> struct RPCIdTraits<fb::fbSpawnActorReq> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::SPAWN_ACTOR_REQ; };
    template<> struct RPCIdTraits<fb::fbSpawnActorRes> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::SPAWN_ACTOR_RES; };
    template<> struct RPCIdTraits<fb::fbDespawnActorReq> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::DESPAWN_ACTOR_REQ; };
    template<> struct RPCIdTraits<fb::fbDespawnActorRes> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::DESPAWN_ACTOR_RES; };

    template<> struct RPCIdTraits<fb::fbPossessActorReq> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::POSSESS_ACTOR_REQ; };
    template<> struct RPCIdTraits<fb::fbPossessActorRes> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::POSSESS_ACTOR_RES; };
    template<> struct RPCIdTraits<fb::fbUnpossessActorReq> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::UNPOSSESS_ACTOR_REQ; };
    template<> struct RPCIdTraits<fb::fbUnpossessActorRes> { static constexpr bool registered = true; static constexpr uint16 value = rpc_id::UNPOSSESS_ACTOR_RES; };
}
