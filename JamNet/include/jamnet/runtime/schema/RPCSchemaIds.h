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

		constexpr uint16 kTcpBindReq            = 0x0101;
		constexpr uint16 kTcpBindRes            = 0x0102;
		constexpr uint16 kUdpBindReq            = 0x0103;
		constexpr uint16 kUdpBindRes            = 0x0104;

		constexpr uint16 kWorldActionReq		= 0x0111;
		constexpr uint16 kWorldActionRes		= 0x0112;

		constexpr uint16 kSpawnActionReq        = 0x0201;
		constexpr uint16 kSpawnActorRes         = 0x0202;
		constexpr uint16 kDespawnActorReq       = 0x0203;
		constexpr uint16 kDespawnActorRes       = 0x0204;

		constexpr uint16 kPossessActorReq       = 0x0211;
		constexpr uint16 kPossessActorRes       = 0x0212;
		constexpr uint16 kUnpossessActorReq     = 0x0213;
		constexpr uint16 kUnpossessActorRes     = 0x0214;

		constexpr uint16 ALL[] = {
			kTcpBindReq, kTcpBindRes, kUdpBindReq, kUdpBindRes,
			kWorldActionReq, kWorldActionRes,
			kSpawnActionReq, kSpawnActorRes, kDespawnActorReq, kDespawnActorRes,
			kPossessActorReq, kPossessActorRes, kUnpossessActorReq, kUnpossessActorRes
		};

		static_assert(AreUnique(ALL), "RPC ids must be unique.");
	}

	template<> struct RPCIdTraits<fb::fbTcpBindReq>			{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kTcpBindReq; };
	template<> struct RPCIdTraits<fb::fbTcpBindRes>			{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kTcpBindRes; };
	template<> struct RPCIdTraits<fb::fbUdpBindReq>			{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kUdpBindReq; };
	template<> struct RPCIdTraits<fb::fbUdpBindRes>			{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kUdpBindRes; };

	template<> struct RPCIdTraits<fb::fbWorldActionReq>		{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kWorldActionReq; };
	template<> struct RPCIdTraits<fb::fbWorldActionRes>		{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kWorldActionRes; };

	template<> struct RPCIdTraits<fb::fbSpawnActorReq>		{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kSpawnActionReq; };
	template<> struct RPCIdTraits<fb::fbSpawnActorRes>		{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kSpawnActorRes; };
	template<> struct RPCIdTraits<fb::fbDespawnActorReq>	{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kDespawnActorReq; };
	template<> struct RPCIdTraits<fb::fbDespawnActorRes>	{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kDespawnActorRes; };

	template<> struct RPCIdTraits<fb::fbPossessActorReq>	{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kPossessActorReq; };
	template<> struct RPCIdTraits<fb::fbPossessActorRes>	{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kPossessActorRes; };
	template<> struct RPCIdTraits<fb::fbUnpossessActorReq>	{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kUnpossessActorReq; };
	template<> struct RPCIdTraits<fb::fbUnpossessActorRes>	{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kUnpossessActorRes; };
}
