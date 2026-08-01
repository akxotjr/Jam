#include "pch.h"

#include "jamnet/runtime/world/data/WorldConfigResolver.h"

namespace jam::net
{


	WorldConfigResolver::WorldConfigResolver(const SharedDataManifest* manifest, WorldTemplateDatabase* tmplDB, WorldArchetypeDatabase* archDB)
	{
		m_manifest   = manifest;
		m_templates  = tmplDB;
		m_archetypes = archDB;
	}


	WorldConfig WorldConfigResolver::ResolveWorldConfig(const WorldInstanceRef& instance) const
	{
		if (!instance.IsValid() || !m_archetypes || !m_templates)
			return {};

		const auto* worldArchetypeData = m_archetypes->Find(instance.archetypeKey);
		if (!worldArchetypeData)
			return {};

		const auto* tmpl = m_templates->Find(worldArchetypeData->templateKey);
		if (!tmpl)
			return {};

		WorldConfig config{};
		config.world.instance	= instance;
		config.templateData		= *tmpl;

		if (!m_manifest || m_manifest->actorArchetypeDatabasePath.empty())
			return {};

		if (!worldArchetypeData->actorLevelName.empty())
		{
			config.actorLevelPath = ResolveActorLevelPath(worldArchetypeData->actorLevelName);
			if (config.actorLevelPath.empty())
				return {};
		}

		if (!worldArchetypeData->physicsAssetName.empty())
		{
			config.physicsAssetPath = ResolvePhysicsAssetPath(worldArchetypeData->physicsAssetName);
			if (config.physicsAssetPath.empty())
				return {};
		}

		return config;
	}

	std::string WorldConfigResolver::ResolveActorLevelPath(const std::string& name) const
	{
		if (!m_manifest) return "";
		return m_manifest->actorLevelDatabasePaths.contains(name) ? m_manifest->actorLevelDatabasePaths.at(name) : "";
	}

	std::string WorldConfigResolver::ResolvePhysicsAssetPath(const std::string& name) const
	{
		if (!m_manifest) return "";
		return m_manifest->physicsAssetDatabasePaths.contains(name) ? m_manifest->physicsAssetDatabasePaths.at(name) : "";
	}

}
