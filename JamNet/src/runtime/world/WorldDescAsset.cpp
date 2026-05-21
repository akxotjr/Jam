#include "pch.h"
#include "jamnet/runtime/world/WorldDescAsset.h"

#include <fstream>
#include <stdexcept>

namespace jam::net
{
	namespace
	{
		inline constexpr const char* kVersion			= "version";
		inline constexpr const char* kDescMap			= "desc_map";

		inline constexpr const char* kDescId			= "desc_id";
		inline constexpr const char* kKind				= "kind";
		inline constexpr const char* kGroup				= "group";

		inline constexpr const char* kPersistent		= "persistent";
		inline constexpr const char* kDestroyWhenEmpty	= "destroy_when_empty";
		inline constexpr const char* kIsPrivate			= "is_private";
		inline constexpr const char* kAllowMultipleInstancePerUser = "allow_multiple_instance_per_user";
		inline constexpr const char* kMaxAuxiliaryWorldMemberships = "max_auxiliary_world_memberships";
		inline constexpr const char* kCapacity			= "capacity";
		inline constexpr const char* kTickMode			= "tick_mode";
		inline constexpr const char* kRoute				= "route";
		inline constexpr const char* kLevelKey			= "level_key";
		inline constexpr const char* kPhysicsProfile	= "physics_profile";

		inline constexpr const char* kRoutePolicy		= "policy";
		inline constexpr const char* kPreferredShard	= "preferred_shard";
		inline constexpr const char* kColocateWorldInstanceId = "colocate_world_instance_id";
		inline constexpr const char* kHardAffinity		= "hard_affinity";

		eWorldKind ParseWorldKind(const nlohmann::json& j)
		{
			if (j.is_number_unsigned())
				return ToEnum<eWorldKind>(j.get<uint8>());

			const std::string value = j.get<std::string>();
			if (value == "virtual")	 return eWorldKind::Virtual;
			if (value == "physical") return eWorldKind::Physical;
			throw std::runtime_error("invalid world desc kind: " + value);
		}

		const char* ToString(eWorldKind kind)
		{
			return (kind == eWorldKind::Virtual) ? "virtual" : "physical";
		}

		eWorldTickMode ParseWorldTickMode(const nlohmann::json& j)
		{
			if (j.is_number_unsigned())
				return ToEnum<eWorldTickMode>(j.get<uint8>());

			const std::string value = j.get<std::string>();
			if (value == "none")		return eWorldTickMode::None;
			if (value == "fixed")		return eWorldTickMode::Fixed;
			if (value == "on_demand")	return eWorldTickMode::OnDemand;
			throw std::runtime_error("invalid world tick mode: " + value);
		}

		const char* ToString(eWorldTickMode tickMode)
		{
			switch (tickMode)
			{
			case eWorldTickMode::None:		return "none";
			case eWorldTickMode::Fixed:		return "fixed";
			case eWorldTickMode::OnDemand:	return "on_demand";
			}
			return nullptr;
		}

		eWorldRoutePolicy ParseWorldRoutePolicy(const nlohmann::json& j)
		{
			if (j.is_number_unsigned())
				return ToEnum<eWorldRoutePolicy>(j.get<uint8>());

			const std::string value = j.get<std::string>();
			if (value == "spread_by_load")		return eWorldRoutePolicy::SpreadByLoad;
			if (value == "preferred_shard")		return eWorldRoutePolicy::PreferredShard;
			if (value == "dedicated_shard")		return eWorldRoutePolicy::DedicatedShard;
			if (value == "colocate_with_world") return eWorldRoutePolicy::CoLocateWithWorld;
			throw std::runtime_error("invalid world route policy: " + value);
		}

		const char* ToString(eWorldRoutePolicy policy)
		{
			switch (policy)
			{
			case eWorldRoutePolicy::SpreadByLoad:		return "spread_by_load";
			case eWorldRoutePolicy::PreferredShard:		return "preferred_shard";
			case eWorldRoutePolicy::DedicatedShard:		return "dedicated_shard";
			case eWorldRoutePolicy::CoLocateWithWorld:	return "colocate_with_world";
			default: return "spread_by_load";
			}
		}


		WorldRouteConfig ParseWorldRouteConfig(const nlohmann::json& j)
		{
			WorldRouteConfig route{};
			if (j.is_null())
				return route;

			if (j.contains(kRoutePolicy))
				route.policy = ParseWorldRoutePolicy(j.at(kRoutePolicy));

			route.preferredShard	= j.value(kPreferredShard, 0u);
			route.colocateWorldId	= j.value(kColocateWorldInstanceId, kInvalidNetWorldId);
			route.hardAffinity		= j.value(kHardAffinity, false);
			return route;
		}

		WorldDesc ParseWorldDesc(const nlohmann::json& j)
		{
			WorldDesc desc{};
			if (j.contains(kKind))
				desc.kind = ParseWorldKind(j.at(kKind));
			desc.group						  = j.value(kGroup, kInvalidWorldGroup);
			desc.allowMultipleInstancePerUser = j.value(kAllowMultipleInstancePerUser, false);
			desc.persistent					  = j.value(kPersistent, true);
			desc.destroyWhenEmpty			  = j.value(kDestroyWhenEmpty, false);
			desc.isPrivate					  = j.value(kIsPrivate, false);
			desc.maxAuxiliaryWorldMemberships = j.value(kMaxAuxiliaryWorldMemberships, 0u);
			desc.capacity					  = j.value(kCapacity, 0u);
			if (j.contains(kTickMode))
				desc.tickMode = ParseWorldTickMode(j.at(kTickMode));
			if (j.contains(kRoute))
				desc.route = ParseWorldRouteConfig(j.at(kRoute));
			desc.levelKey		= j.value(kLevelKey, std::string{});
			desc.physicsProfile	= j.value(kPhysicsProfile, std::string{});
			return desc;
		}

		nlohmann::json ToJson(uint32 descId, const WorldDesc& desc)
		{
			nlohmann::json j;
			j[kKind]						 = ToString(desc.kind);
			j[kGroup]						 = desc.group;
			j[kDescId]						 = descId;
			j[kAllowMultipleInstancePerUser] = desc.allowMultipleInstancePerUser;
			j[kPersistent]					 = desc.persistent;
			j[kDestroyWhenEmpty]			 = desc.destroyWhenEmpty;
			j[kIsPrivate]					 = desc.isPrivate;
			j[kMaxAuxiliaryWorldMemberships] = desc.maxAuxiliaryWorldMemberships;
			j[kCapacity]					 = desc.capacity;
			j[kTickMode]					 = ToString(desc.tickMode);
			if (!desc.levelKey.empty())
				j[kLevelKey] = desc.levelKey;
			if (!desc.physicsProfile.empty())
				j[kPhysicsProfile] = desc.physicsProfile;

			j[kRoute] =
			{
				{ kRoutePolicy,				ToString(desc.route.policy) },
				{ kPreferredShard,			desc.route.preferredShard	},
				{ kColocateWorldInstanceId, desc.route.colocateWorldId	},
				{ kHardAffinity,			desc.route.hardAffinity		},
			};
			return j;
		}
	}

	const WorldDesc* WorldDescAsset::Find(uint32 descId) const
	{
		auto it = descMap.find(descId);
		return (it != descMap.end()) ? &it->second : nullptr;
	}

	WorldConfig WorldDescAsset::MakeConfig(const WorldKey& key) const
	{
		const WorldDesc* desc = Find(key.descId);
		if (!desc)
			return {};

		return WorldConfig
		{
			.key  = key,
			.desc = *desc,
		};
	}

	WorldDescIO::json WorldDescIO::LoadJson(const std::string& path)
	{
		std::ifstream ifs(path);
		if (!ifs.is_open())
			throw std::runtime_error("failed to open world desc file for read: " + path);

		json j;
		ifs >> j;
		ValidateJson(j);
		return j;
	}

	void WorldDescIO::SaveJson(const std::string& path, const json& json)
	{
		ValidateJson(json);

		std::ofstream ofs(path);
		if (!ofs.is_open())
			throw std::runtime_error("failed to open world desc file for write: " + path);

		ofs << json.dump(2);
	}

	WorldDescAsset WorldDescIO::LoadWorldDescAsset(const std::string& path)
	{
		return LoadWorldDescAssetFromJson(LoadJson(path));
	}

	WorldDescAsset WorldDescIO::LoadWorldDescAssetFromJson(const json& json)
	{
		ValidateJson(json);

		WorldDescAsset asset{};
		asset.version = json.value(kVersion, 1);
		if (asset.version != 1)
			throw std::runtime_error("unsupported world desc asset version");

		const auto& descMap = json.at(kDescMap);
		asset.descMap.reserve(descMap.size());
		for (const auto& item : descMap)
		{
			uint32	  descId = item.at(kDescId).get<uint32>();
			WorldDesc desc   = ParseWorldDesc(item);
			auto [_, inserted] = asset.descMap.emplace(descId, std::move(desc));
			if (!inserted)
				throw std::runtime_error("duplicate world desc key");
		}

		return asset;
	}

	WorldDescIO::json WorldDescIO::ToJson(const WorldDescAsset& asset)
	{
		json j;
		j[kVersion] = asset.version;
		j[kDescMap] = json::array();
		for (const auto& [key, desc] : asset.descMap)
			j[kDescMap].push_back(net::ToJson(key, desc));
		return j;
	}

	const char* WorldDescIO::SchemaPath()
	{
		return "world_desc_asset.schema.json";
	}

	const WorldDescIO::json& WorldDescIO::SchemaJson()
	{
		static const json schema =
		{
			{ "type", "object" },
			{ "required", json::array({ kVersion, kDescMap }) },
			{ "properties",
				{
					{ kVersion, { { "type", "integer" } } },
					{ kDescMap,  { { "type", "array"   } } },
				}
			},
		};
		return schema;
	}

	void WorldDescIO::ValidateJson(const json& json)
	{
		if (!json.is_object())
			throw std::runtime_error("world desc asset must be a json object");
		if (!json.contains(kVersion) || !json.at(kVersion).is_number_integer())
			throw std::runtime_error("world desc asset requires integer version");
		if (!json.contains(kDescMap) || !json.at(kDescMap).is_array() || json.at(kDescMap).empty())
			throw std::runtime_error("world desc asset requires non-empty desc_map array");
	}
}
