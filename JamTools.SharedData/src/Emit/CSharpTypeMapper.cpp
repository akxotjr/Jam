#include "Emit/CSharpTypeMapper.h"
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
				return "int";
			case eSchemaScalarHint::I64:
				return "long";
			case eSchemaScalarHint::U32:
				return "uint";
			case eSchemaScalarHint::U64:
				return "ulong";
			case eSchemaScalarHint::None:
				break;
			}

			switch (primitiveType.primitive)
			{
			case eSchemaPrimitiveKind::String:
				return "string";
			case eSchemaPrimitiveKind::Integer:
				return "int";
			case eSchemaPrimitiveKind::Number:
				return "float";
			case eSchemaPrimitiveKind::Boolean:
				return "bool";
			}

			throw std::runtime_error("Unsupported primitive schema type for C# emitter.");
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
				return "List<" + MapFieldScopedType(owner, field, *arrayType.elementType) + ">";
			}

			case eSchemaTypeKind::ObjectRef:
				return static_cast<const SchemaObjectRefType&>(type).targetName;

			case eSchemaTypeKind::Enum:
				return CSharpTypeMapper::MakeFieldLocalEnumTypeName(owner, field);

			case eSchemaTypeKind::Map:
			{
				const auto& mapType = static_cast<const SchemaMapType&>(type);
				return "Dictionary<string, " + MapFieldScopedType(owner, field, *mapType.valueType) + ">";
			}

			case eSchemaTypeKind::Polymorphic:
				return static_cast<const SchemaPolymorphicType&>(type).baseTypeName;

			case eSchemaTypeKind::Custom:
				return "JToken";
			}

			throw std::runtime_error("Unsupported schema type for C# emitter.");
		}
	}

	std::string CSharpTypeMapper::MakeFieldLocalEnumTypeName(const SchemaObject& owner, const SchemaField& field)
	{
		return "e" + owner.name + NameConverter::ToCSharpTypeName(field.jsonName);
	}

	std::string CSharpTypeMapper::MapType(const SchemaType& type)
	{
		switch (type.kind)
		{
		case eSchemaTypeKind::Primitive:
		{
			const auto& primitive = static_cast<const SchemaPrimitiveType&>(type);
			return MapPrimitiveType(primitive);
		}

		case eSchemaTypeKind::Array:
		{
			const auto& array =
				static_cast<const SchemaArrayType&>(type);

			return "List<" + MapType(*array.elementType) + ">";
		}

		case eSchemaTypeKind::ObjectRef:
		{
			const auto& objectRef =
				static_cast<const SchemaObjectRefType&>(type);

			return objectRef.targetName;
		}

		case eSchemaTypeKind::Enum:
			throw std::runtime_error("Field-local enum requires field context for C# type mapping.");

		case eSchemaTypeKind::Map:
		{
			const auto& mapType = static_cast<const SchemaMapType&>(type);
			return "Dictionary<string, " + MapType(*mapType.valueType) + ">";
		}

		case eSchemaTypeKind::Polymorphic:
		{
			const auto& polymorphicType = static_cast<const SchemaPolymorphicType&>(type);
			return polymorphicType.baseTypeName;
		}

		case eSchemaTypeKind::Custom:
			return "JToken";

		default:
			break;
		}

		throw std::runtime_error("Unsupported schema type for C# emitter.");
	}

	std::string CSharpTypeMapper::MapFieldType(const SchemaObject& owner, const SchemaField& field)
	{
		return MapFieldScopedType(owner, field, *field.type);
	}
}
