#pragma once
#include <memory>
#include <string>
#include <vector>

namespace jam::tool
{
	enum class eSchemaTypeKind
	{
		Primitive,	// string, integer, number, boolean
		Array,		// array of another type
		ObjectRef,	// reference to an object type defined in the same document
		Enum,		// string enum type, with a list of values
		Map,		// string-keyed map with a schema-defined value type
		Polymorphic,	// oneOf object union with discriminator-driven polymorphism
		Custom		// schema-defined opaque/custom mapped type
	};

	enum class eSchemaCustomTypeKind
	{
		Opaque,
		HandleRef
	};

	enum class eSchemaPrimitiveKind
	{
		String,
		Integer,
		Number,
		Boolean
	};

	enum class eSchemaScalarHint
	{
		None,
		F32,
		F64,
		I32,
		I64,
		U32,
		U64
	};

	struct SchemaType
	{
		explicit SchemaType(eSchemaTypeKind kind)
			: kind(kind)
		{}

		virtual ~SchemaType() = default;

		eSchemaTypeKind kind;
	};

	struct SchemaPrimitiveType final : SchemaType
	{
		explicit SchemaPrimitiveType(
			eSchemaPrimitiveKind primitive,
			eSchemaScalarHint scalarHint = eSchemaScalarHint::None)
			: SchemaType(eSchemaTypeKind::Primitive), primitive(primitive), scalarHint(scalarHint)
		{}

		eSchemaPrimitiveKind primitive;
		eSchemaScalarHint scalarHint;
	};

	struct SchemaArrayType final : SchemaType
	{
		explicit SchemaArrayType(std::unique_ptr<SchemaType> elementType)
			: SchemaType(eSchemaTypeKind::Array), elementType(std::move(elementType))
		{}

		std::unique_ptr<SchemaType> elementType;
	};

	struct SchemaObjectRefType final : SchemaType
	{    
		explicit SchemaObjectRefType(std::string targetName)
			: SchemaType(eSchemaTypeKind::ObjectRef), targetName(std::move(targetName))
		{}

		std::string targetName;
	};

	struct SchemaEnumType final : SchemaType
	{
		explicit SchemaEnumType(std::vector<std::string> values)
			: SchemaType(eSchemaTypeKind::Enum), values(std::move(values))
		{}

		std::vector<std::string> values;
	};

	struct SchemaMapType final : SchemaType
	{
		explicit SchemaMapType(std::unique_ptr<SchemaType> valueType)
			: SchemaType(eSchemaTypeKind::Map), valueType(std::move(valueType))
		{}

		std::unique_ptr<SchemaType> valueType;
	};

	struct SchemaPolymorphicBranch
	{
		std::string objectTypeName;
		std::string discriminatorValue;
	};

	struct SchemaPolymorphicType final : SchemaType
	{
		SchemaPolymorphicType(
			std::string baseTypeName,
			std::string discriminatorField,
			std::vector<SchemaPolymorphicBranch> branches)
			: SchemaType(eSchemaTypeKind::Polymorphic)
			, baseTypeName(std::move(baseTypeName))
			, discriminatorField(std::move(discriminatorField))
			, branches(std::move(branches))
		{}

		std::string baseTypeName;
		std::string discriminatorField;
		std::vector<SchemaPolymorphicBranch> branches;
	};

	struct SchemaCustomType final : SchemaType
	{
		SchemaCustomType(
			std::string cppTypeName,
			eSchemaCustomTypeKind customKind = eSchemaCustomTypeKind::Opaque)
			: SchemaType(eSchemaTypeKind::Custom)
			, cppTypeName(std::move(cppTypeName))
			, customKind(customKind)
		{}

		std::string cppTypeName;
		eSchemaCustomTypeKind customKind;
	};
}
