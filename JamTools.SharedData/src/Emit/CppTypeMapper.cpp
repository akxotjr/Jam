#include "Emit/CppTypeMapper.h"
#include "Naming/NameConverter.h"

#include <stdexcept>

namespace jam::tool
{
	namespace
	{
		std::string MapFieldScopedType(const SchemaObject& owner, const SchemaField& field, const SchemaType& type);

		std::string MapPrimitiveType(const SchemaPrimitiveType& primitiveType)
		{
			switch (primitiveType.scalarHint)
			{
			case eSchemaScalarHint::F32:
				return "float";
			case eSchemaScalarHint::F64:
				return "double";
			case eSchemaScalarHint::I32:
				return "int32_t";
			case eSchemaScalarHint::I64:
				return "int64_t";
			case eSchemaScalarHint::U32:
				return "uint32_t";
			case eSchemaScalarHint::U64:
				return "uint64_t";
			case eSchemaScalarHint::None:
				break;
			}

			switch (primitiveType.primitive)
			{
			case eSchemaPrimitiveKind::String:
				return "std::string";
			case eSchemaPrimitiveKind::Integer:
				return "int32_t";
			case eSchemaPrimitiveKind::Number:
				return "float";
			case eSchemaPrimitiveKind::Boolean:
				return "bool";
			}

			throw std::runtime_error("Unsupported primitive schema type");
		}

		std::string MapFieldScopedType(const SchemaObject& owner, const SchemaField& field, const SchemaType& type)
		{
			switch (type.kind)
			{
			case eSchemaTypeKind::Primitive:
				return MapPrimitiveType(static_cast<const SchemaPrimitiveType&>(type));

			case eSchemaTypeKind::Array:
			{
				const auto& arrayType = static_cast<const SchemaArrayType&>(type);
				return "std::vector<" + MapFieldScopedType(owner, field, *arrayType.elementType) + ">";
			}

			case eSchemaTypeKind::ObjectRef:
				return static_cast<const SchemaObjectRefType&>(type).targetName;

			case eSchemaTypeKind::Enum:
				return CppTypeMapper::MakeFieldLocalEnumTypeName(owner, field);

			case eSchemaTypeKind::Map:
			{
				const auto& mapType = static_cast<const SchemaMapType&>(type);
				return "std::unordered_map<std::string, " + MapFieldScopedType(owner, field, *mapType.valueType) + ">";
			}

			case eSchemaTypeKind::Polymorphic:
			{
				const auto& polymorphicType = static_cast<const SchemaPolymorphicType&>(type);
				return "std::unique_ptr<" + polymorphicType.baseTypeName + ">";
			}

			case eSchemaTypeKind::Custom:
				return static_cast<const SchemaCustomType&>(type).cppTypeName;
			}

			throw std::runtime_error("Unsupported schema type");
		}
	}

	std::string CppTypeMapper::MakeFieldLocalEnumTypeName(const SchemaObject& owner, const SchemaField& field)
	{
		return "e" + owner.name + NameConverter::ToCppTypeName(field.jsonName);
	}

	std::string CppTypeMapper::MapType(const SchemaType& type)
	{
		switch (type.kind)
		{
		case eSchemaTypeKind::Primitive:
		{
			const auto& primitiveType = static_cast<const SchemaPrimitiveType&>(type);
			return MapPrimitiveType(primitiveType);
		}

		case eSchemaTypeKind::Array:
		{
			const auto& arrayType = static_cast<const SchemaArrayType&>(type);
			return "std::vector<" + MapType(*arrayType.elementType) + ">";
		}

		case eSchemaTypeKind::ObjectRef:
		{
			const auto& objectRefType = static_cast<const SchemaObjectRefType&>(type);
			return objectRefType.targetName;
		}

		case eSchemaTypeKind::Enum:
			throw std::runtime_error("Field-local enum requires field context for C++ type mapping.");

		case eSchemaTypeKind::Map:
		{
			const auto& mapType = static_cast<const SchemaMapType&>(type);
			return "std::unordered_map<std::string, " + MapType(*mapType.valueType) + ">";
		}

		case eSchemaTypeKind::Polymorphic:
		{
			const auto& polymorphicType = static_cast<const SchemaPolymorphicType&>(type);
			return "std::unique_ptr<" + polymorphicType.baseTypeName + ">";
		}

		case eSchemaTypeKind::Custom:
		{
			const auto& customType = static_cast<const SchemaCustomType&>(type);
			return customType.cppTypeName;
		}

		default:
			break;
		}

		throw std::runtime_error("Unsupported schema type");
	}

	std::string CppTypeMapper::MapFieldType(const SchemaObject& owner, const SchemaField& field)
	{
		return MapFieldScopedType(owner, field, *field.type);
	}
}
