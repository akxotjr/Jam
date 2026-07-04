#include "Validation/SchemaSemanticValidator.h"

#include "Naming/NameConverter.h"

namespace jam::tool
{
	bool SchemaSemanticValidator::Validate(const SchemaDocument& document, DiagnosticBag& diagnostics) const
	{
		std::unordered_set<std::string> objectNames;

		for (const auto& object : document.objects)
		{
			if (!objectNames.insert(object.name).second)
			{
				ReportError(
					diagnostics,
					diag::semantic::DuplicateObjectName,
					document,
					"Duplicate object name after normalization: " + object.name);
			}
		}

		for (const auto& object : document.objects)
		{
			ValidateObject(document, object, objectNames, diagnostics);
		}

		return !diagnostics.HasError();
	}

	void SchemaSemanticValidator::ValidateObject(
		const SchemaDocument& document,
		const SchemaObject& object,
		const std::unordered_set<std::string>& objectNames,
		DiagnosticBag& diagnostics) const
	{
		std::unordered_set<std::string> jsonNames;
		std::unordered_set<std::string> cppNames;
		std::unordered_set<std::string> csharpNames;

		for (const auto& field : object.fields)
		{
			if (!jsonNames.insert(field.jsonName).second)
			{
				ReportError(
					diagnostics,
					diag::semantic::DuplicateFieldJsonName,
					document,
					"Duplicate field json name '" + field.jsonName + "' in object '" + object.name + "'.");
			}

			if (!cppNames.insert(field.cppName).second)
			{
				ReportError(
					diagnostics,
					diag::semantic::DuplicateFieldCppName,
					document,
					"Duplicate normalized C++ field name '" + field.cppName + "' in object '" + object.name + "'.");
			}

			if (!csharpNames.insert(field.csharpName).second)
			{
				ReportError(
					diagnostics,
					diag::semantic::DuplicateFieldCSharpName,
					document,
					"Duplicate normalized C# field name '" + field.csharpName + "' in object '" + object.name + "'.");
			}

			if (!field.type)
			{
				ReportError(
					diagnostics,
					diag::semantic::FieldTypeMissing,
					document,
					"Field '" + field.jsonName + "' in object '" + object.name + "' has no parsed type.");
				continue;
			}

			ValidateType(document, object, field, *field.type, objectNames, diagnostics);
		}

		for (const auto& requiredFieldName : object.requiredFieldNames)
		{
			if (!jsonNames.contains(requiredFieldName))
			{
				ReportError(
					diagnostics,
					diag::semantic::RequiredFieldMissing,
					document,
					"Required field '" + requiredFieldName + "' is not declared in schema object '" + object.name + "'.");
			}
		}

		for (const auto& conflictFieldName : object.flattenedFieldConflicts)
		{
			ReportError(
				diagnostics,
				diag::semantic::AllOfFieldConflict,
				document,
				"Duplicate field '" + conflictFieldName + "' while flattening schema object '" + object.name + "'.");
		}
	}

	void SchemaSemanticValidator::ValidateType(
		const SchemaDocument& document,
		const SchemaObject& owner,
		const SchemaField& field,
		const SchemaType& type,
		const std::unordered_set<std::string>& objectNames,
		DiagnosticBag& diagnostics) const
	{
		switch (type.kind)
		{
		case eSchemaTypeKind::Primitive:
			return;

		case eSchemaTypeKind::Array:
		{
			const auto& arrayType = static_cast<const SchemaArrayType&>(type);
			if (!arrayType.elementType)
			{
				ReportError(
					diagnostics,
					diag::semantic::ArrayElementTypeMissing,
					document,
					"Array field '" + field.jsonName + "' in object '" + owner.name + "' has no element type.");
				return;
			}

			ValidateType(document, owner, field, *arrayType.elementType, objectNames, diagnostics);
			return;
		}

		case eSchemaTypeKind::ObjectRef:
		{
			const auto& objectRefType = static_cast<const SchemaObjectRefType&>(type);
			if (!objectNames.contains(objectRefType.targetName))
			{
				ReportError(
					diagnostics,
					diag::semantic::ObjectRefTargetMissing,
					document,
					"Field '" + field.jsonName + "' in object '" + owner.name + "' references missing object type '" + objectRefType.targetName + "'.");
			}
			return;
		}

		case eSchemaTypeKind::Enum:
			ValidateEnumType(document, owner, field, static_cast<const SchemaEnumType&>(type), diagnostics);
			return;

		case eSchemaTypeKind::Map:
		{
			const auto& mapType = static_cast<const SchemaMapType&>(type);
			if (!mapType.valueType)
			{
				ReportError(
					diagnostics,
					diag::semantic::MapValueTypeMissing,
					document,
					"Map field '" + field.jsonName + "' in object '" + owner.name + "' has no value type.");
				return;
			}

			ValidateType(document, owner, field, *mapType.valueType, objectNames, diagnostics);
			return;
		}

		case eSchemaTypeKind::Polymorphic:
			ValidatePolymorphicType(document, owner, field, static_cast<const SchemaPolymorphicType&>(type), objectNames, diagnostics);
			return;

		case eSchemaTypeKind::Custom:
			return;
		}
	}

	void SchemaSemanticValidator::ValidateEnumType(
		const SchemaDocument& document,
		const SchemaObject& owner,
		const SchemaField& field,
		const SchemaEnumType& enumType,
		DiagnosticBag& diagnostics) const
	{
		if (enumType.values.empty())
		{
			ReportError(
				diagnostics,
				diag::semantic::EnumEmpty,
				document,
				"Enum field '" + field.jsonName + "' in object '" + owner.name + "' must define at least one value.");
			return;
		}

		std::unordered_set<std::string> rawValues;
		std::unordered_set<std::string> cppMemberNames;
		std::unordered_set<std::string> csharpMemberNames;

		for (const auto& value : enumType.values)
		{
			if (!rawValues.insert(value).second)
			{
				ReportError(
					diagnostics,
					diag::semantic::EnumDuplicateValue,
					document,
					"Enum field '" + field.jsonName + "' in object '" + owner.name + "' contains duplicate value '" + value + "'.");
			}

			const std::string cppMemberName = NameConverter::ToCppEnumMemberName(value);
			if (!cppMemberNames.insert(cppMemberName).second)
			{
				ReportError(
					diagnostics,
					diag::semantic::EnumDuplicateCppMember,
					document,
					"Enum field '" + field.jsonName + "' in object '" + owner.name + "' produces duplicate C++ enum member '" + cppMemberName + "'.");
			}

			const std::string csharpMemberName = NameConverter::ToCSharpEnumMemberName(value);
			if (!csharpMemberNames.insert(csharpMemberName).second)
			{
				ReportError(
					diagnostics,
					diag::semantic::EnumDuplicateCSharpMember,
					document,
					"Enum field '" + field.jsonName + "' in object '" + owner.name + "' produces duplicate C# enum member '" + csharpMemberName + "'.");
			}
		}
	}

	void SchemaSemanticValidator::ValidatePolymorphicType(
		const SchemaDocument& document,
		const SchemaObject& owner,
		const SchemaField& field,
		const SchemaPolymorphicType& polymorphicType,
		const std::unordered_set<std::string>& objectNames,
		DiagnosticBag& diagnostics) const
	{
		if (polymorphicType.baseTypeName.empty())
		{
			ReportError(
				diagnostics,
				diag::semantic::PolymorphicBaseNameEmpty,
				document,
				"Polymorphic field '" + field.jsonName + "' in object '" + owner.name + "' has an empty base type name.");
		}

		if (polymorphicType.discriminatorField.empty())
		{
			ReportError(
				diagnostics,
				diag::semantic::PolymorphicDiscriminatorEmpty,
				document,
				"Polymorphic field '" + field.jsonName + "' in object '" + owner.name + "' has an empty discriminator field.");
		}

		if (polymorphicType.branches.empty())
		{
			ReportError(
				diagnostics,
				diag::semantic::PolymorphicBranchesEmpty,
				document,
				"Polymorphic field '" + field.jsonName + "' in object '" + owner.name + "' must define at least one branch.");
			return;
		}

		std::unordered_set<std::string> branchObjectNames;
		std::unordered_set<std::string> discriminatorValues;

		for (const auto& branch : polymorphicType.branches)
		{
			if (!branchObjectNames.insert(branch.objectTypeName).second)
			{
				ReportError(
					diagnostics,
					diag::semantic::PolymorphicDuplicateBranchObject,
					document,
					"Polymorphic field '" + field.jsonName + "' in object '" + owner.name + "' contains duplicate branch object '" + branch.objectTypeName + "'.");
			}

			if (!objectNames.contains(branch.objectTypeName))
			{
				ReportError(
					diagnostics,
					diag::semantic::PolymorphicBranchTargetMissing,
					document,
					"Polymorphic field '" + field.jsonName + "' in object '" + owner.name + "' references missing branch object '" + branch.objectTypeName + "'.");
			}

			if (branch.discriminatorValue.empty())
			{
				ReportError(
					diagnostics,
					diag::semantic::PolymorphicDiscriminatorValueEmpty,
					document,
					"Polymorphic field '" + field.jsonName + "' in object '" + owner.name + "' contains an empty discriminator value.");
			}
			else if (!discriminatorValues.insert(branch.discriminatorValue).second)
			{
				ReportError(
					diagnostics,
					diag::semantic::PolymorphicDuplicateDiscriminatorValue,
					document,
					"Polymorphic field '" + field.jsonName + "' in object '" + owner.name + "' contains duplicate discriminator value '" + branch.discriminatorValue + "'.");
			}
		}
	}

	void SchemaSemanticValidator::ReportError(
		DiagnosticBag& diagnostics,
		std::string code,
		const SchemaDocument& document,
		std::string message) const
	{
		Diagnostic diagnostic;
		diagnostic.severity = eDiagnosticSeverity::Error;
		diagnostic.code		= std::move(code);
		diagnostic.message	= std::move(message);
		diagnostic.file		= document.srcPath.string();
		diagnostic.line		= 0;
		diagnostic.column	= 0;
		diagnostics.Add(std::move(diagnostic));
	}
}
