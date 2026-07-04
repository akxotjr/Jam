#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "Core/Diagnostic.h"

namespace jam::tool
{
	struct SchemaValidationOptions
	{
		std::string sourceName;
		std::string schemaPath;
	};

	class JsonSchemaValidator
	{
	public:
		bool Validate(const nlohmann::json& root, DiagnosticBag& diagnostics, const SchemaValidationOptions& options = {}) const;
	};
}
