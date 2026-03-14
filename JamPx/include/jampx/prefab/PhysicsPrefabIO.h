#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

namespace jam::px
{
	using nlohmann::json;
	using nlohmann::json_schema::json_validator;

	class PhysicsPrefabIO
	{
	public:
		static json              LoadPrefabJsonFromFile(const std::string& path);
		static void              SavePrefabJsonToFile(const std::string& path, const json& prefab);

		static PhysicsAsset      LoadPrefabAssetFromFile(const std::string& path);
		static PhysicsAsset      LoadPrefabAssetFromJson(const json& root);

		static json              LoadLevelJsonFromFile(const std::string& path);
		static void              SaveLevelJsonToFile(const std::string& path, const json& level);

		static PhysicsLevelAsset LoadLevelAssetFromFile(const std::string& path);
		static PhysicsLevelAsset LoadLevelAssetFromJson(const json& root);

#if JAMPX_WITH_EDITOR
		static const char*		 PrefabSchemaPath();
		static const json&		 PrefabSchemaJson();
		static void              ValidatePrefabJson(const json& prefab);

		static const char*		 LevelSchemaPath();
		static const json&		 LevelSchemaJson();
		static void              ValidateLevelJson(const json& level);
#endif
	};
}