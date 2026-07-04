#pragma once
#include <string>
#include <vector>

#include "SchemaField.h"

namespace jam::tool
{
	struct SchemaObject
	{
		std::string					name;
		std::string					description;
		std::vector<std::string>	requiredFieldNames;
		std::vector<std::string>	flattenedFieldConflicts;
		std::vector<SchemaField>	fields;
	};
} // namespace jam::tools
