#pragma once

#include "jamnet/runtime/world/data/SharedDataCatalog.h"
#include "jamnet/runtime/world/data/WorldArchetypeDatabase.h"
#include "jamnet/runtime/world/data/WorldTemplateDatabase.h"

#include <string>
#include <string_view>

namespace jam::net
{
	class WorldConfigResolver
	{
	public:
		void BindContentRootPath(std::string_view contentRootPath) { m_contentRootPath = std::string(contentRootPath); }
		void BindContentRootFromDataPath(std::string_view dataPath);
		void BindSharedDataCatalog(const SharedDataCatalog* catalog) { m_catalog = catalog; }
		void BindWorldArchetypeDatabase(const WorldArchetypeDatabase* database) { m_archetypes = database; }
		void BindWorldTemplateDatabase(const WorldTemplateDatabase* database) { m_templates = database; }

		WorldConfig ResolveWorldConfig(const WorldKey& key) const;

	private:
		std::string ResolveActorArchetypeSetPath(std::string_view name) const;
		std::string ResolveActorLevelPath(std::string_view name) const;
		std::string ResolvePhysicsAssetPath(std::string_view name) const;
		std::string ResolveCatalogEntryPath(const SharedDataCatalogEntry* entry) const;

	private:
		std::string					  m_contentRootPath;
		const SharedDataCatalog* m_catalog		= nullptr;
		const WorldArchetypeDatabase*	  m_archetypes  = nullptr;
		const WorldTemplateDatabase*	  m_templates	= nullptr;
	};
}
