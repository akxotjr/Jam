#include "Validation/JsonSchemaValidator.h"

#include <fstream>

#include <nlohmann/json-schema.hpp>

#include "Core/DiagnosticCodes.h"

namespace jam::tool
{
	namespace
	{
		bool LoadSchemaFile(
			const std::string& schemaPath,
			nlohmann::json& schemaRoot,
			DiagnosticBag& diagnostics)
		{
			std::ifstream file(schemaPath);
			if (!file.is_open())
			{
				diagnostics.Error(
					diag::schema::ValidationSchemaNotFound,
					"Could not open validation schema file: " + schemaPath);
				return false;
			}

			try
			{
				file >> schemaRoot;
				return true;
			}
			catch (const std::exception& e)
			{
				diagnostics.Error(diag::schema::ValidationSchemaParse, "Failed to parse validation schema file '" + schemaPath + "': " + std::string(e.what()));
				return false;
			}
		}
	}

	bool JsonSchemaValidator::Validate(
		const nlohmann::json& root,
		DiagnosticBag& diagnostics,
		const SchemaValidationOptions& options) const
	{
		if (options.schemaPath.empty())
		{
			diagnostics.Error(
				diag::schema::MetaInvalid,
				"Schema validation schema path is not configured.");
			return false;
		}

		try
		{
			nlohmann::json validationSchema;
			if (!LoadSchemaFile(options.schemaPath, validationSchema, diagnostics))
				return false;

			nlohmann::json_schema::json_validator validator;
			validator.set_root_schema(validationSchema);
			(void)validator.validate(root);
			return true;
		}
		catch (const std::exception& e)
		{
			const std::string sourceName = options.sourceName.empty() ? "<schema>" : options.sourceName;
			diagnostics.Error(diag::schema::MetaInvalid, "Schema validation failed for '" + sourceName + "': " + std::string(e.what()));
			return false;
		}
	}
}
