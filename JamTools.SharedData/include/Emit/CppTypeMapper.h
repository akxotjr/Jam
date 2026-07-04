#pragma once
#include <string>

#include "AST/SchemaField.h"
#include "AST/SchemaObject.h"
#include "AST/SchemaType.h"

namespace jam::tool
{
	class CppTypeMapper
	{
	public:
		static std::string MapType(const SchemaType& type);
		static std::string MapFieldType(const SchemaObject& owner, const SchemaField& field);
		static std::string MakeFieldLocalEnumTypeName(const SchemaObject& owner, const SchemaField& field);
	};
}
