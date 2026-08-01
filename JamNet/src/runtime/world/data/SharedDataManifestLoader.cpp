#include "pch.h"

#include "jamnet/runtime/world/data/SharedDataManifestLoader.h"

#include <Cpp/shared_data_manifest.generated.hpp>

#include <stdexcept>

namespace jam::net
{
	namespace
	{
		std::string ParentPathOf(std::string_view path)
		{
			const size_t pos = path.find_last_of("/\\");
			return pos == std::string_view::npos ? std::string{} : std::string(path.substr(0, pos));
		}

		std::string JoinPath(std::string_view root, std::string_view relative)
		{
			if (relative.empty())
				return {};
			if (root.empty())
				return std::string(relative);

			std::string result(root);
			if (result.back() != '/' && result.back() != '\\')
				result.push_back('/');
			result.append(relative);
			return result;
		}

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

		void ValidateEntryName(const std::string& name, const char* sectionName)
		{
			if (name.empty())
				throw std::runtime_error(std::string("shared data manifest entry name must not be empty: ") + sectionName);
		}

		void ValidateEntryPath(const std::string& name, std::string_view path, const char* sectionName)
		{
			if (!IsRelativeJsonPath(path))
				throw std::runtime_error(std::string("shared data manifest path must be relative .json path: ") + sectionName + "." + name);
		}
	}

	jam::shared::gen::SharedDataManifestRootDto SharedDataManifestLoader::LoadDto(const std::string& path)
	{
		return jam::shared::gen::LoadSharedDataManifestRootDto(path);
	}

	SharedDataManifest SharedDataManifestBuilder::Build(const jam::shared::gen::SharedDataManifestRootDto& dto)
	{
		SharedDataManifest manifest{};
		if (dto.version != 1)
			throw std::runtime_error("unsupported shared data manifest asset version");

		ValidateEntryPath("world_templates", dto.bootstrap.worldTemplates.path, "bootstrap");
		ValidateEntryPath("world_archetypes", dto.bootstrap.worldArchetypes.path, "bootstrap");
		ValidateEntryPath("world_instances", dto.bootstrap.worldInstances.path, "bootstrap");
		ValidateEntryPath("actor_archetypes", dto.bootstrap.actorArchetypes.path, "bootstrap");

		manifest.worldTemplateDatabasePath  = dto.bootstrap.worldTemplates.path;
		manifest.worldArchetypeDatabasePath = dto.bootstrap.worldArchetypes.path;
		manifest.worldInstanceDatabasePath  = dto.bootstrap.worldInstances.path;
		manifest.actorArchetypeDatabasePath = dto.bootstrap.actorArchetypes.path;
		for (const auto& [name, entry] : dto.content.actorLevelSet)
		{
			ValidateEntryName(name, "content.actor_level_set");
			ValidateEntryPath(name, entry.path, "content.actor_level_set");
			manifest.actorLevelDatabasePaths.emplace(name, entry.path);
		}
		for (const auto& [name, entry] : dto.content.physicsAssetSet)
		{
			ValidateEntryName(name, "content.physics_asset_set");
			ValidateEntryPath(name, entry.path, "content.physics_asset_set");
			manifest.physicsAssetDatabasePaths.emplace(name, entry.path);
		}

		return manifest;
	}

	SharedDataManifest SharedDataManifestLoader::Load(const std::string& path)
	{
		SharedDataManifest manifest = SharedDataManifestBuilder::Build(LoadDto(path));
		manifest.contentRootPath = ParentPathOf(path);
		manifest.worldTemplateDatabasePath  = JoinPath(manifest.contentRootPath, manifest.worldTemplateDatabasePath);
		manifest.worldArchetypeDatabasePath = JoinPath(manifest.contentRootPath, manifest.worldArchetypeDatabasePath);
		manifest.worldInstanceDatabasePath  = JoinPath(manifest.contentRootPath, manifest.worldInstanceDatabasePath);
		manifest.actorArchetypeDatabasePath = JoinPath(manifest.contentRootPath, manifest.actorArchetypeDatabasePath);
		for (auto& entryPath : manifest.actorLevelDatabasePaths | std::views::values)
			entryPath = JoinPath(manifest.contentRootPath, entryPath);
		for (auto& entryPath : manifest.physicsAssetDatabasePaths | std::views::values)
			entryPath = JoinPath(manifest.contentRootPath, entryPath);

		return manifest;
	}
}
