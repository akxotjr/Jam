#pragma once

#include "jamnet/runtime/world/WorldActionTypes.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

#include <string>
#include <unordered_map>

namespace jam::net
{
	struct WorldDescAsset
	{
		int32 version = 1;
		std::unordered_map<uint32, WorldDesc> descMap;

		const WorldDesc*	Find(uint32 descId) const;
		WorldConfig			MakeConfig(const WorldKey& key) const;
	};


	struct WorldDescIO
	{
		using json			 = nlohmann::json;
		using json_validator = nlohmann::json_schema::json_validator;

	public:
		static json				LoadJson(const std::string& path);
		static void				SaveJson(const std::string& path, const json& json);

		static WorldDescAsset	LoadWorldDescAsset(const std::string& path);
		static WorldDescAsset	LoadWorldDescAssetFromJson(const json& json);
		static json				ToJson(const WorldDescAsset& asset);

		static const char*		SchemaPath();
		static const json&		SchemaJson();
		static void				ValidateJson(const json& json);
	};


} // namespace jam::net
