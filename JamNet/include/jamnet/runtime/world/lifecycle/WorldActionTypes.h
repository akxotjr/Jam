#pragma once

#include "jamnet/runtime/world/lifecycle/WorldIdentity.h"
#include "jamnet/runtime/world/lifecycle/WorldTemplateKey.h"

#include <string>

namespace jam::net
{
	// Shard-local object handle retained only while the client-world execution
	// path is migrated to its fixed shard. It is not a world identity and must
	// not appear in new client/server contracts.
	using WorldGroup     = uint64;

	inline constexpr WorldGroup		kInvalidWorldGroup = 0;
	inline constexpr WorldGroup		kDefaultWorldGroup = 1;

	enum class eWorldRuntimeState : uint8
	{
		Absent		= 0,
		Creating	= 1,
		Standby		= 2,
		Active		= 3,
		Paused		= 4,
		Draining	= 5,
		Destroying	= 6,
		Failed		= 7,
	};

	enum class eWorldRoutePolicy : uint8
	{
		SpreadByLoad		= 0,
		PreferredShard		= 1,
		DedicatedShard		= 2,
		CoLocateWithWorld	= 3,
	};

	struct WorldRouteConfig
	{
		eWorldRoutePolicy	policy			= eWorldRoutePolicy::SpreadByLoad;
		uint32				preferredShard	= 0;
		WorldId				colocateWorldId = kInvalidWorldId;
		bool				hardAffinity	= false;
	};


	// settings shared by all instances of the same world type.
	struct WorldTemplateData
	{
		std::string			name;
		WorldTemplateKey	key;
		WorldGroup			group						 = kInvalidWorldGroup;
		bool				allowMultipleInstancePerUser = false;
		bool				persistent					 = true;
		bool				pauseWhenNoActivePresence	 = true;
		bool				destroyWhenEmpty			 = false;
		bool				isPrivate					 = false;
		uint64				standbyTTL_ns				 = 0_ns;
		uint64				pausedTTL_ns				 = 0_ns;
		uint32				capacity					 = 0;
		WorldRouteConfig	route						 = {};
	};

	// Concrete instance config resolved from authored instance + live world address.
	struct WorldConfig
	{
		WorldRef		world		 = {};
		WorldTemplateData	templateData = {};

		std::string			actorLevelPath;
		std::string			physicsAssetPath;

		bool IsValid() const { return world.instance.IsValid() && !physicsAssetPath.empty(); }
		bool HasWorld() const { return world.worldId != kInvalidWorldId; }
		WorldRef GetWorldRef() const { return world; }
		bool operator==(const WorldConfig& rhs) const
		{
			return world == rhs.world;
		}
	};


	struct ClientBindState
	{
		bool	tcpBound	= false;
		bool	udpBound	= false;
		bool	ready		= false;
	};


}
