#pragma once

#include <string>
#include <unordered_set>

#include "AST/SchemaDocument.h"
#include "Core/DiagnosticCodes.h"
#include "Core/Diagnostic.h"

namespace jam::tool
{
	class SchemaSemanticValidator
	{
	public:
		bool Validate(const SchemaDocument& document, DiagnosticBag& diagnostics) const;

	private:
		void ValidateObject(
			const SchemaDocument& document,
			const SchemaObject& object,
			const std::unordered_set<std::string>& objectNames,
			DiagnosticBag& diagnostics) const;
		void ValidateType(
			const SchemaDocument& document,
			const SchemaObject& owner,
			const SchemaField& field,
			const SchemaType& type,
			const std::unordered_set<std::string>& objectNames,
			DiagnosticBag& diagnostics) const;
		void ValidateEnumType(
			const SchemaDocument& document,
			const SchemaObject& owner,
			const SchemaField& field,
			const SchemaEnumType& enumType,
			DiagnosticBag& diagnostics) const;
		void ValidatePolymorphicType(
			const SchemaDocument& document,
			const SchemaObject& owner,
			const SchemaField& field,
			const SchemaPolymorphicType& polymorphicType,
			const std::unordered_set<std::string>& objectNames,
			DiagnosticBag& diagnostics) const;
		void ReportError(
			DiagnosticBag& diagnostics,
			std::string code,
			const SchemaDocument& document,
			std::string message) const;
	};
}
