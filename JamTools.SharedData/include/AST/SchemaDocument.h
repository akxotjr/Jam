#pragma once

#include <filesystem>
#include <vector>
#include <string>

#include "SchemaObject.h"

namespace jam::tool
{
	struct SchemaDocument
	{
		std::filesystem::path		srcPath;
		std::string					outputName;
		std::vector<SchemaObject>	objects;
	};
}
