#pragma once
#include <filesystem>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "Core/Diagnostic.h"
#include "AST/SchemaDocument.h"

namespace jam::tool
{

	class JsonSchemaParser
	{
	public:
		explicit JsonSchemaParser() = default;
		~JsonSchemaParser() = default;

		SchemaDocument					ParseFile(const std::filesystem::path& srcPath, DiagnosticBag& diagnostics) const;

	private:
		SchemaObject					ParseObject(
			const std::string& objectName,
			bool isRootObject,
			const nlohmann::json& node,
			const nlohmann::json& documentRoot,
			const std::filesystem::path& schemaPath,
			DiagnosticBag& diagnostics) const;
		void							AppendObjectFields(
			SchemaObject& object,
			const nlohmann::json& node,
			const nlohmann::json& documentRoot,
			const std::filesystem::path& schemaPath,
			DiagnosticBag& diagnostics) const;
		void							MergeField(
			SchemaObject& object,
			SchemaField field,
			DiagnosticBag& diagnostics) const;
		SchemaField						ParseField(
			const std::string& jsonName,
			const nlohmann::json& node,
			const nlohmann::json& documentRoot,
			const std::filesystem::path& schemaPath,
			bool required,
			DiagnosticBag& diagnostics) const;
		std::unique_ptr<SchemaType>		ParseType(
			const nlohmann::json& node,
			const nlohmann::json& documentRoot,
			const std::filesystem::path& schemaPath,
			DiagnosticBag& diagnostics) const;
	};
}
