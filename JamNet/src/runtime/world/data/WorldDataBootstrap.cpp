#include "pch.h"

#include "jamnet/runtime/world/data/WorldDataBootstrap.h"
#include "jamnet/runtime/world/data/WorldArchetypesLoader.h"
#include "jamnet/runtime/world/data/SharedDataCatalogLoader.h"
#include "jamnet/runtime/world/data/WorldTemplatesLoader.h"

namespace jam::net
{
	WorldDataBootstrapBundle WorldDataBootstrap::Load(const WorldDataBootstrapPaths& paths)
	{
		WorldDataBootstrapBundle bundle{};
		bundle.catalog = SharedDataCatalogLoader::Load(paths.sharedDataCatalogPath);
		bundle.templates = WorldTemplatesLoader::Load(paths.worldTemplatePath);
		bundle.archetypes = WorldArchetypesLoader::Load(paths.worldArchetypePath);

		bundle.resolver.BindSharedDataCatalog(&bundle.catalog);
		bundle.resolver.BindWorldTemplateDatabase(&bundle.templates);
		bundle.resolver.BindWorldArchetypeDatabase(&bundle.archetypes);
		bundle.resolver.BindContentRootFromDataPath(paths.sharedDataCatalogPath);
		return bundle;
	}
}
