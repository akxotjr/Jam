#pragma once
#include <memory>
#include <string>

#include "SchemaType.h"

namespace jam::tool
{
	struct SchemaField
	{
		std::string					jsonName;
		std::string					cppName;
		std::string					csharpName;
		std::unique_ptr<SchemaType>	type;
		bool						required = false;
		std::string					description;
	};
}
