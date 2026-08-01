#pragma once

#include "jamnet/core/net/RPC.h"
#include "jamnet/runtime/protocol/schema/gen/binding_handshake_generated.h"
#include "jamnet/runtime/protocol/schema/gen/actor_spawn_generated.h"

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

		constexpr uint16 kSpawnActorReq         = 0x0201;
		constexpr uint16 kSpawnActorRes         = 0x0202;
		constexpr uint16 kDespawnActorReq       = 0x0203;
		constexpr uint16 kDespawnActorRes       = 0x0204;
		constexpr uint16 kSpawnPlayerReq        = 0x0205;
		constexpr uint16 kSpawnPlayerRes        = 0x0206;
		constexpr uint16 kDespawnPlayerReq      = 0x0207;
		constexpr uint16 kDespawnPlayerRes      = 0x0208;

		constexpr uint16 ALL[] = {
			kTcpBindReq, kTcpBindRes, kUdpBindReq, kUdpBindRes,
			kSpawnActorReq, kSpawnActorRes, kDespawnActorReq, kDespawnActorRes,
			kSpawnPlayerReq, kSpawnPlayerRes, kDespawnPlayerReq, kDespawnPlayerRes
		};

		static_assert(AreUnique(ALL), "RPC ids must be unique.");
	}

	template<> struct RPCIdTraits<fb::fbTcpBindReq>			{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kTcpBindReq; };
	template<> struct RPCIdTraits<fb::fbTcpBindRes>			{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kTcpBindRes; };
	template<> struct RPCIdTraits<fb::fbUdpBindReq>			{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kUdpBindReq; };
	template<> struct RPCIdTraits<fb::fbUdpBindRes>			{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kUdpBindRes; };

	template<> struct RPCIdTraits<fb::fbSpawnActorReq>		{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kSpawnActorReq; };
	template<> struct RPCIdTraits<fb::fbSpawnActorRes>		{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kSpawnActorRes; };
	template<> struct RPCIdTraits<fb::fbDespawnActorReq>	{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kDespawnActorReq; };
	template<> struct RPCIdTraits<fb::fbDespawnActorRes>	{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kDespawnActorRes; };
	template<> struct RPCIdTraits<fb::fbSpawnPlayerReq>		{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kSpawnPlayerReq; };
	template<> struct RPCIdTraits<fb::fbSpawnPlayerRes>		{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kSpawnPlayerRes; };
	template<> struct RPCIdTraits<fb::fbDespawnPlayerReq>	{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kDespawnPlayerReq; };
	template<> struct RPCIdTraits<fb::fbDespawnPlayerRes>	{ static constexpr bool registered = true; static constexpr uint16 value = rpc_id::kDespawnPlayerRes; };

}
