#include "pch.h"

#include "jambase/JsonFileIO.h"
#include "jamnet/runtime/world/data/SharedDataCatalogLoader.h"

#include <Cpp/shared_data_catalog.generated.hpp>

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace jam::net
{
	namespace
	{
		using CatalogEntryDto = jam::shared::gen::CatalogEntryDto;
		using CatalogEntryDtoMap = std::unordered_map<std::string, CatalogEntryDto>;
		using EntryByNameMap = std::unordered_map<std::string, SharedDataCatalogEntry>;
		using EntryByKeyMap = std::unordered_map<SharedDataCatalogKey, const SharedDataCatalogEntry*>;

		bool IsRelativeJsonPath(std::string_view path) noexcept
		{
			if (path.empty() || path.size() < 5)
				return false;
			if (path[0] == '/' || path[0] == '\\')
				return false;
			if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
				return false;
			if (path.find("..") != std::string_view::npos)
				return false;
			if (path.substr(path.size() - 5) != ".json")
				return false;
			return true;
		}

		void ValidateCatalogIdentity(const std::string& name, SharedDataCatalogKey key)
		{
			if (name.empty())
				throw std::runtime_error("shared data catalog entry name must not be empty");

			const auto expectedKey = MakeSharedDataCatalogKey(name);
			if (!IsValidAssetKey(expectedKey))
				throw std::runtime_error("shared data catalog key must not be zero: " + name);
			if (expectedKey != key)
				throw std::runtime_error("shared data catalog key mismatch: " + name);
		}

		void ValidateCatalogPath(const std::string& name, std::string_view path)
		{
			if (!IsRelativeJsonPath(path))
				throw std::runtime_error("shared data catalog path must be relative .json path: " + name);
		}

		SharedDataCatalogEntry BuildEntry(const std::string& name, const CatalogEntryDto& dto)
		{
			SharedDataCatalogEntry result{};
			result.name = name;
			result.key = SharedDataCatalogKey::FromU64(dto.key);
			result.path = dto.path;
			ValidateCatalogIdentity(result.name, result.key);
			ValidateCatalogPath(result.name, result.path);
			return result;
		}

		void BuildSection(
			const CatalogEntryDtoMap& dtoEntries,
			const char* sectionName,
			EntryByNameMap& entriesByName,
			EntryByKeyMap& entriesByKey)
		{
			entriesByName.clear();
			entriesByKey.clear();
			entriesByName.reserve(dtoEntries.size());
			entriesByKey.reserve(dtoEntries.size());

			for (const auto& [name, dtoEntry] : dtoEntries)
			{
				SharedDataCatalogEntry entry = BuildEntry(name, dtoEntry);
				auto [it, inserted] = entriesByName.emplace(entry.name, std::move(entry));
				if (!inserted)
					throw std::runtime_error(std::string("duplicate shared data catalog name: ") + name);

				const SharedDataCatalogEntry* stored = &it->second;
				if (!entriesByKey.emplace(stored->key, stored).second)
					throw std::runtime_error(std::string("duplicate shared data catalog key in ") + sectionName + ": " + std::to_string(stored->key.v));
			}
		}
	}

	SharedDataCatalogLoader::json SharedDataCatalogLoader::LoadJson(const std::string& path)
	{
		return JsonFileIO::Load(path, "failed to open shared data catalog file for read: ",
			[](const json&)
			{
			});
	}

	jam::shared::gen::SharedDataCatalogRootDto SharedDataCatalogLoader::LoadDto(const std::string& path)
	{
		return jam::shared::gen::LoadSharedDataCatalogRootDto(path);
	}

	jam::shared::gen::SharedDataCatalogRootDto SharedDataCatalogLoader::LoadDtoFromJson(const json& json)
	{
		return jam::shared::gen::DeserializeSharedDataCatalogRootDto(json);
	}

	SharedDataCatalog SharedDataCatalogLoader::Load(const std::string& path)
	{
		return SharedDataCatalogBuilder::Build(LoadDto(path));
	}

	SharedDataCatalog SharedDataCatalogBuilder::Build(const jam::shared::gen::SharedDataCatalogRootDto& dto)
	{
		SharedDataCatalog catalog{};
		catalog.version = dto.version;
		if (catalog.version != 1)
			throw std::runtime_error("unsupported shared data catalog asset version");

		BuildSection(dto.actorArchetypeSets, "actor_archetype_sets", catalog.actorArchetypeSetsByName, catalog.actorArchetypeSetsByKey);
		BuildSection(dto.physicsAssets, "physics_assets", catalog.physicsAssetsByName, catalog.physicsAssetsByKey);
		BuildSection(dto.actorLevels, "actor_levels", catalog.actorLevelsByName, catalog.actorLevelsByKey);

		return catalog;
	}
}
