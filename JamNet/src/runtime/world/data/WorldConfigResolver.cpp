#include "pch.h"

#include "jamnet/runtime/world/data/WorldConfigResolver.h"

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
	}

	void WorldConfigResolver::BindContentRootFromDataPath(std::string_view dataPath)
	{
		m_contentRootPath = ParentPathOf(dataPath);
	}

	WorldConfig WorldConfigResolver::ResolveWorldConfig(const WorldKey& key) const
	{
		if (!key.IsValid() || !m_archetypes || !m_templates)
			return {};

		const auto* archetype = m_archetypes->Find(key.archetypeKey);
		if (!archetype)
			return {};

		const auto* tmpl = m_templates->Find(archetype->templateKey);
		if (!tmpl)
			return {};

		WorldConfig config{};
		config.key = key;
		config.templateData = *tmpl;

		if (!archetype->actorArchetypesName.empty())
		{
			config.templateData.actorArchetypeSetPath = ResolveActorArchetypeSetPath(archetype->actorArchetypesName);
			if (config.templateData.actorArchetypeSetPath.empty())
				return {};
		}

		if (!archetype->actorLevelName.empty())
		{
			config.templateData.actorLevelPath = ResolveActorLevelPath(archetype->actorLevelName);
			if (config.templateData.actorLevelPath.empty())
				return {};
		}

		if (!archetype->physicsAssetName.empty())
		{
			config.templateData.physicsAssetPath = ResolvePhysicsAssetPath(archetype->physicsAssetName);
			if (config.templateData.physicsAssetPath.empty())
				return {};
		}

		return config;
	}

	std::string WorldConfigResolver::ResolveActorArchetypeSetPath(std::string_view name) const
	{
		return ResolveCatalogEntryPath(name.empty() || !m_catalog ? nullptr : m_catalog->FindActorArchetypeSet(name));
	}

	std::string WorldConfigResolver::ResolvePhysicsAssetPath(std::string_view name) const
	{
		return ResolveCatalogEntryPath(name.empty() || !m_catalog ? nullptr : m_catalog->FindPhysicsAsset(name));
	}

	std::string WorldConfigResolver::ResolveActorLevelPath(std::string_view name) const
	{
		return ResolveCatalogEntryPath(name.empty() || !m_catalog ? nullptr : m_catalog->FindActorLevel(name));
	}

	std::string WorldConfigResolver::ResolveCatalogEntryPath(const SharedDataCatalogEntry* entry) const
	{
		return entry ? JoinPath(m_contentRootPath, entry->path) : std::string{};
	}
}
