#pragma once

#include <string>

namespace jam::tool
{
	class NameConverter
	{
	public:
		static std::string ToPascalCase(std::string_view name);
		static std::string ToCamelCase(std::string_view name);
		static std::string ToSnakeCase(std::string_view name);

		static std::string ToCppFieldName(std::string_view jsonName);
		static std::string ToCppTypeName(std::string_view schemaName);
		static std::string ToGeneratedDtoTypeName(std::string_view schemaName);
		static std::string ToGeneratedRootDtoTypeName(std::string_view schemaName);
		static std::string ToCppEnumMemberName(std::string_view enumValue);

		static std::string ToCSharpPropertyName(std::string_view jsonName);
		static std::string ToCSharpTypeName(std::string_view schemaName);
		static std::string ToCSharpEnumMemberName(std::string_view enumValue);
	};

}
