#pragma once

#include "jamnet/runtime/world/lifecycle/WorldIdentity.h"
#include "jamnet/runtime/world/lifecycle/WorldActionTypes.h"
#include "jamnet/runtime/world/data/SharedDataManifest.h"
#include "jamnet/runtime/world/data/WorldArchetypeDatabase.h"
#include "jamnet/runtime/world/data/WorldTemplateDatabase.h"

#include <string>


namespace jam::net
{
	class WorldConfigResolver
	{
	public:
		WorldConfigResolver(const SharedDataManifest* manifest, WorldTemplateDatabase* tmplDB, WorldArchetypeDatabase* archDB);
		~WorldConfigResolver() = default;

		WorldConfig ResolveWorldConfig(const WorldInstanceRef& instance) const;

	private:
		std::string ResolveActorLevelPath(const std::string& name) const;
		std::string ResolvePhysicsAssetPath(const std::string& name) const;

	private:
		const SharedDataManifest*	  m_manifest	= nullptr;
		const WorldArchetypeDatabase* m_archetypes  = nullptr;
		const WorldTemplateDatabase*  m_templates	= nullptr;
	};
}
