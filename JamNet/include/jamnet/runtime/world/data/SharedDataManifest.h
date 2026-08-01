#pragma once


#include <string>
#include <unordered_map>

namespace jam::net
{

	struct SharedDataManifest
	{
		std::string contentRootPath;

		// bootstrap data paths
		std::string worldTemplateDatabasePath;
		std::string worldArchetypeDatabasePath;
		std::string worldInstanceDatabasePath;
		std::string actorArchetypeDatabasePath;

		// content data paths
		std::unordered_map<std::string, std::string> physicsAssetDatabasePaths;
		std::unordered_map<std::string, std::string> actorLevelDatabasePaths;
	};


}
