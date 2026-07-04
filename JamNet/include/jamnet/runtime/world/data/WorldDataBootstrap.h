#pragma once

#include "jamnet/runtime/world/data/SharedDataCatalog.h"
#include "jamnet/runtime/world/data/WorldArchetypeDatabase.h"
#include "jamnet/runtime/world/data/WorldConfigResolver.h"
#include "jamnet/runtime/world/data/WorldTemplateDatabase.h"

#include <string>

namespace jam::net
{
	struct WorldDataBootstrapPaths
	{
		std::string sharedDataCatalogPath;
		std::string worldTemplatePath;
		std::string worldArchetypePath;
	};

	struct WorldDataBootstrapBundle
	{
		SharedDataCatalog catalog = {};
		WorldTemplateDatabase templates = {};
		WorldArchetypeDatabase archetypes = {};
		WorldConfigResolver resolver = {};
	};

	struct WorldDataBootstrap
	{
		static WorldDataBootstrapBundle Load(const WorldDataBootstrapPaths& paths);
	};
}
