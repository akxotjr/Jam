#pragma once

namespace jam::tool::diag
{
	namespace schema
	{
		inline constexpr const char* FileNotFound				= "SCHEMA_FILE_NOT_FOUND";
		inline constexpr const char* JsonParse					= "SCHEMA_JSON_PARSE";
		inline constexpr const char* UnsupportedKeyword			= "SCHEMA_UNSUPPORTED_KEYWORD";
		inline constexpr const char* OneOfBranchProperties		= "SCHEMA_ONEOF_BRANCH_PROPERTIES";
		inline constexpr const char* OneOfDiscriminatorField	= "SCHEMA_ONEOF_DISCRIMINATOR_FIELD";
		inline constexpr const char* OneOfDiscriminatorValue	= "SCHEMA_ONEOF_DISCRIMINATOR_VALUE";
		inline constexpr const char* ScalarHintType				= "SCHEMA_SCALAR_HINT_TYPE";
		inline constexpr const char* ScalarHintUnknown			= "SCHEMA_SCALAR_HINT_UNKNOWN";
		inline constexpr const char* ScalarHintMismatch			= "SCHEMA_SCALAR_HINT_MISMATCH";
		inline constexpr const char* CustomCppTypeType			= "SCHEMA_CUSTOM_CPP_TYPE_TYPE";
		inline constexpr const char* CustomHandleRefType		= "SCHEMA_CUSTOM_HANDLE_REF_TYPE";
		inline constexpr const char* CustomCppTypeEmpty			= "SCHEMA_CUSTOM_CPP_TYPE_EMPTY";
		inline constexpr const char* AllOfArray					= "SCHEMA_ALLOF_ARRAY";
		inline constexpr const char* AllOfRef					= "SCHEMA_ALLOF_REF";
		inline constexpr const char* AllOfConditionalUnsupported = "SCHEMA_ALLOF_CONDITIONAL_UNSUPPORTED";
		inline constexpr const char* AllOfEntryUnsupported		= "SCHEMA_ALLOF_ENTRY_UNSUPPORTED";
		inline constexpr const char* ObjectType					= "SCHEMA_OBJECT_TYPE";
		inline constexpr const char* Properties					= "SCHEMA_PROPERTIES";
		inline constexpr const char* OneOfUnsupported			= "SCHEMA_ONEOF_UNSUPPORTED";
		inline constexpr const char* OneOfBaseName				= "SCHEMA_ONEOF_BASE_NAME";
		inline constexpr const char* OneOfDiscriminator			= "SCHEMA_ONEOF_DISCRIMINATOR";
		inline constexpr const char* OneOfArray					= "SCHEMA_ONEOF_ARRAY";
		inline constexpr const char* OneOfType					= "SCHEMA_ONEOF_TYPE";
		inline constexpr const char* OneOfBranchRef				= "SCHEMA_ONEOF_BRANCH_REF";
		inline constexpr const char* OneOfBranchResolve			= "SCHEMA_ONEOF_BRANCH_RESOLVE";
		inline constexpr const char* EnumType					= "SCHEMA_ENUM_TYPE";
		inline constexpr const char* EnumArray					= "SCHEMA_ENUM_ARRAY";
		inline constexpr const char* EnumValue					= "SCHEMA_ENUM_VALUE";
		inline constexpr const char* RefResolve					= "SCHEMA_REF_RESOLVE";
		inline constexpr const char* MissingType				= "SCHEMA_MISSING_TYPE";
		inline constexpr const char* ArrayItems					= "SCHEMA_ARRAY_ITEMS";
		inline constexpr const char* InlineObject				= "SCHEMA_INLINE_OBJECT";
		inline constexpr const char* MapAdditionalProperties	= "SCHEMA_MAP_ADDITIONAL_PROPERTIES";
		inline constexpr const char* ObjectShape				= "SCHEMA_OBJECT_SHAPE";
		inline constexpr const char* UnknownType				= "SCHEMA_UNKNOWN_TYPE";
		inline constexpr const char* ValidationSchemaNotFound	= "SCHEMA_VALIDATION_SCHEMA_NOT_FOUND";
		inline constexpr const char* ValidationSchemaParse		= "SCHEMA_VALIDATION_SCHEMA_PARSE";
		inline constexpr const char* MetaInvalid				= "SCHEMA_META_INVALID";
	}

	namespace semantic
	{
		inline constexpr const char* DuplicateObjectName = "SEMANTIC_DUPLICATE_OBJECT_NAME";
		inline constexpr const char* DuplicateFieldJsonName = "SEMANTIC_DUPLICATE_FIELD_JSON_NAME";
		inline constexpr const char* DuplicateFieldCppName = "SEMANTIC_DUPLICATE_FIELD_CPP_NAME";
		inline constexpr const char* DuplicateFieldCSharpName = "SEMANTIC_DUPLICATE_FIELD_CSHARP_NAME";
		inline constexpr const char* FieldTypeMissing = "SEMANTIC_FIELD_TYPE_MISSING";
		inline constexpr const char* ArrayElementTypeMissing = "SEMANTIC_ARRAY_ELEMENT_TYPE_MISSING";
		inline constexpr const char* ObjectRefTargetMissing = "SEMANTIC_OBJECT_REF_TARGET_MISSING";
		inline constexpr const char* MapValueTypeMissing = "SEMANTIC_MAP_VALUE_TYPE_MISSING";
		inline constexpr const char* EnumEmpty = "SEMANTIC_ENUM_EMPTY";
		inline constexpr const char* EnumDuplicateValue = "SEMANTIC_ENUM_DUPLICATE_VALUE";
		inline constexpr const char* EnumDuplicateCppMember = "SEMANTIC_ENUM_DUPLICATE_CPP_MEMBER";
		inline constexpr const char* EnumDuplicateCSharpMember				= "SEMANTIC_ENUM_DUPLICATE_CSHARP_MEMBER";
		inline constexpr const char* PolymorphicBaseNameEmpty				= "SEMANTIC_POLYMORPHIC_BASE_NAME_EMPTY";
		inline constexpr const char* PolymorphicDiscriminatorEmpty			= "SEMANTIC_POLYMORPHIC_DISCRIMINATOR_EMPTY";
		inline constexpr const char* PolymorphicBranchesEmpty				= "SEMANTIC_POLYMORPHIC_BRANCHES_EMPTY";
		inline constexpr const char* PolymorphicDuplicateBranchObject		= "SEMANTIC_POLYMORPHIC_DUPLICATE_BRANCH_OBJECT";
		inline constexpr const char* PolymorphicBranchTargetMissing			= "SEMANTIC_POLYMORPHIC_BRANCH_TARGET_MISSING";
		inline constexpr const char* PolymorphicDiscriminatorValueEmpty		= "SEMANTIC_POLYMORPHIC_DISCRIMINATOR_VALUE_EMPTY";
		inline constexpr const char* PolymorphicDuplicateDiscriminatorValue = "SEMANTIC_POLYMORPHIC_DUPLICATE_DISCRIMINATOR_VALUE";
		inline constexpr const char* RequiredFieldMissing					= "SEMANTIC_REQUIRED_FIELD_MISSING";
		inline constexpr const char* AllOfFieldConflict						= "SEMANTIC_ALLOF_FIELD_CONFLICT";
	}
}
