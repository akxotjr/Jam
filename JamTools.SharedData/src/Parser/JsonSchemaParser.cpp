#include "AST/SchemaType.h"
#include "Core/DiagnosticCodes.h"
#include "Naming/NameConverter.h"
#include "Parser/JsonSchemaParser.h"

#include <algorithm>
#include <fstream>

namespace jam::tool
{
	namespace 
	{
		constexpr const char* kScalarHintKeyword				= "x-jam-scalar";
		constexpr const char* kPolymorphicKeyword				= "x-jam-polymorphic";
		constexpr const char* kPolymorphicBaseNameKeyword		= "x-jam-base-name";
		constexpr const char* kPolymorphicDiscriminatorKeyword	= "x-jam-discriminator";
		constexpr const char* kCustomCppTypeKeyword				= "x-jam-cpp-type";
		constexpr const char* kCustomHandleRefKeyword			= "x-jam-handle-ref";

		void ReportUnsupportedKeywords(
			const nlohmann::json& node,
			std::string_view contextName,
			DiagnosticBag& diagnostics)
		{
			static constexpr std::string_view unsupportedKeywords[] =
			{
				"anyOf",
				"not",
				"patternProperties",
				"unevaluatedProperties",
				"prefixItems",
				"contains",
				"dependentSchemas",
				"dependencies"
			};

			for (const auto keyword : unsupportedKeywords)
			{
				if (node.contains(keyword))
				{
					diagnostics.Error(
						diag::schema::UnsupportedKeyword,
						"Unsupported schema keyword '" + std::string(keyword) + "' in " + std::string(contextName) + ".");
				}
			}
		}

		void RecordRequiredFields(
			SchemaObject& object,
			const std::unordered_set<std::string>& requiredSet)
		{
			for (const auto& requiredFieldName : requiredSet)
			{
				if (std::ranges::find(object.requiredFieldNames, requiredFieldName) == object.requiredFieldNames.end())
					object.requiredFieldNames.push_back(requiredFieldName);
			}
		}

		void RecordFieldConflict(
			SchemaObject& object,
			const std::string& fieldName)
		{
			object.flattenedFieldConflicts.push_back(fieldName);
		}

		std::string ResolveRootObjectName(const nlohmann::json& root, const std::filesystem::path& srcPath)
		{
			const std::string stem = srcPath.stem().string();

			if (stem.ends_with(".schema"))
				return stem.substr(0, stem.size() - 7);

			return stem;
		}

		void MoveVersionFieldFirstIfPresent(SchemaObject& object)
		{
			const auto it = std::ranges::find_if(
				object.fields,
				[](const SchemaField& field) { return field.jsonName == "version"; });

			if (it == object.fields.end() || it == object.fields.begin())
				return;

			SchemaField versionField = std::move(*it);
			object.fields.erase(it);
			object.fields.insert(object.fields.begin(), std::move(versionField));
		}

		std::string ResolveRefName(const std::string& ref)
		{
			const std::string prefix = "#/$defs/";
			const std::string escapedPrefix = "#/%24defs/";
			if (ref.starts_with(prefix))
				return ref.substr(prefix.size());
			if (ref.starts_with(escapedPrefix))
				return ref.substr(escapedPrefix.size());
			return ref;
		}

		const nlohmann::json* ResolveDefsObjectNode(const nlohmann::json& documentRoot, const std::string& ref)
		{
			if (!documentRoot.contains("$defs") || !documentRoot.at("$defs").is_object())
				return nullptr;

			const std::string defName = ResolveRefName(ref);
			const auto& defs = documentRoot.at("$defs");

			if (!defs.contains(defName))
				return nullptr;

			return &defs.at(defName);
		}

		bool IsNamedObjectSchema(const nlohmann::json& node)
		{
			if (node.contains("allOf"))
				return true;

			return node.contains("type")
				&& node.at("type") == "object"
				&& node.contains("properties")
				&& node.at("properties").is_object();
		}

		std::string ResolvePolymorphicDiscriminatorValue(
			const nlohmann::json& branchNode,
			const std::string& discriminatorField,
			DiagnosticBag& diagnostics)
		{
			if (!branchNode.contains("properties") || !branchNode.at("properties").is_object())
			{
				diagnostics.Error(
					diag::schema::OneOfBranchProperties,
					"Polymorphic branch must define object properties.");
				return {};
			}

			const auto& properties = branchNode.at("properties");

			if (!properties.contains(discriminatorField))
			{
				diagnostics.Error(
					diag::schema::OneOfDiscriminatorField,
					"Polymorphic branch is missing discriminator field '" + discriminatorField + "'.");
				return {};
			}

			const auto& discriminatorNode = properties.at(discriminatorField);

			if (discriminatorNode.contains("const") && discriminatorNode.at("const").is_string())
				return discriminatorNode.at("const").get<std::string>();

			if (discriminatorNode.contains("enum")
				&& discriminatorNode.at("enum").is_array()
				&& discriminatorNode.at("enum").size() == 1
				&& discriminatorNode.at("enum")[0].is_string())
			{
				return discriminatorNode.at("enum")[0].get<std::string>();
			}

			diagnostics.Error(
				diag::schema::OneOfDiscriminatorValue,
				"Polymorphic discriminator field '" + discriminatorField + "' must define a single string const or enum value.");
			return {};
		}

		std::unordered_set<std::string> ResolveRequiredSet(const nlohmann::json& node)
		{
			std::unordered_set<std::string> requiredSet;
			if (node.contains("required") && node.at("required").is_array())
			{
				for (const auto& item : node.at("required"))
				{
					requiredSet.insert(item.get<std::string>());
				}
			}
			return requiredSet;
		}

		eSchemaScalarHint ParseScalarHintValue(const std::string& scalarHintText)
		{
			if (scalarHintText == "f32") return eSchemaScalarHint::F32;
			if (scalarHintText == "f64") return eSchemaScalarHint::F64;
			if (scalarHintText == "i32") return eSchemaScalarHint::I32;
			if (scalarHintText == "i64") return eSchemaScalarHint::I64;
			if (scalarHintText == "u32") return eSchemaScalarHint::U32;
			if (scalarHintText == "u64") return eSchemaScalarHint::U64;

			return eSchemaScalarHint::None;
		}

		bool IsScalarHintCompatible(eSchemaPrimitiveKind primitiveKind, eSchemaScalarHint scalarHint)
		{
			switch (primitiveKind)
			{
			case eSchemaPrimitiveKind::Number:
				return scalarHint == eSchemaScalarHint::F32 || scalarHint == eSchemaScalarHint::F64;

			case eSchemaPrimitiveKind::Integer:
				return scalarHint == eSchemaScalarHint::I32
					|| scalarHint == eSchemaScalarHint::I64
					|| scalarHint == eSchemaScalarHint::U32
					|| scalarHint == eSchemaScalarHint::U64;

			case eSchemaPrimitiveKind::String:
			case eSchemaPrimitiveKind::Boolean:
				return false;
			}

			return false;
		}

		eSchemaScalarHint ResolveScalarHint(
			const nlohmann::json& node,
			eSchemaPrimitiveKind primitiveKind,
			DiagnosticBag& diagnostics)
		{
			if (!node.contains(kScalarHintKeyword))
				return eSchemaScalarHint::None;

			const auto& scalarHintNode = node.at(kScalarHintKeyword);

			if (!scalarHintNode.is_string())
			{
				diagnostics.Error(diag::schema::ScalarHintType, std::string(kScalarHintKeyword) + " must be a string.");
				return eSchemaScalarHint::None;
			}

			const std::string scalarHintText = scalarHintNode.get<std::string>();
			const eSchemaScalarHint scalarHint = ParseScalarHintValue(scalarHintText);

			if (scalarHint == eSchemaScalarHint::None)
			{
				diagnostics.Error(diag::schema::ScalarHintUnknown, "Unsupported scalar hint: " + scalarHintText);
				return eSchemaScalarHint::None;
			}

			if (!IsScalarHintCompatible(primitiveKind, scalarHint))
			{
				diagnostics.Error(diag::schema::ScalarHintMismatch, "Scalar hint '" + scalarHintText + "' is not compatible with this schema type.");
				return eSchemaScalarHint::None;
			}

			return scalarHint;
		}
	} // anonymous namespace



	SchemaDocument JsonSchemaParser::ParseFile(
		const std::filesystem::path& srcPath,
		DiagnosticBag& diagnostics) const
	{
		std::ifstream file(srcPath);
		if (!file.is_open())
		{
			diagnostics.Error(diag::schema::FileNotFound, "Could not open schema file: " + srcPath.string());
			return SchemaDocument{};
		}

		nlohmann::json root;
		try
		{
			file >> root;
		}
		catch (const std::exception& e)
		{
			Diagnostic diagnostic;
			diagnostic.severity = eDiagnosticSeverity::Error;
			diagnostic.code = diag::schema::JsonParse;
			diagnostic.message = e.what();
			diagnostic.file = srcPath.string();
			diagnostic.line = 0;
			diagnostic.column = 0;
			diagnostics.Add(std::move(diagnostic));
			return SchemaDocument{};
		}

		SchemaDocument document;
		document.srcPath = srcPath;
		const std::string rootName = ResolveRootObjectName(root, srcPath);

		{
			SchemaObject rootObject = ParseObject(rootName, true, root, root, srcPath, diagnostics);
			document.objects.push_back(std::move(rootObject));
		}

		if (root.contains("$defs"))
		{
			const auto& defs = root.at("$defs");

			for (const auto& [defName, defNode] : defs.items())
			{
				if (!IsNamedObjectSchema(defNode))
					continue;

				SchemaObject defObject = ParseObject(defName, false, defNode, root, srcPath, diagnostics);
				document.objects.push_back(std::move(defObject));
			}
		}

		return document;
	}

	SchemaObject JsonSchemaParser::ParseObject(
		const std::string& objectName, 
		bool isRootObject,
		const nlohmann::json& node,
		const nlohmann::json& documentRoot,
		const std::filesystem::path& schemaPath,
		DiagnosticBag& diagnostics) const
	{
		SchemaObject object;
		object.name = isRootObject
			? NameConverter::ToGeneratedRootDtoTypeName(objectName)
			: NameConverter::ToGeneratedDtoTypeName(objectName);
		ReportUnsupportedKeywords(node, "object schema '" + object.name + "'", diagnostics);

		if (node.contains("description"))
			object.description = node.at("description").get<std::string>();

		if (node.contains("allOf"))
		{
			const auto& allOfNode = node.at("allOf");

			if (!allOfNode.is_array())
			{
				diagnostics.Error(diag::schema::AllOfArray, "Schema object '" + objectName + "' must define allOf as an array.");
				return object;
			}

			for (const auto& entry : allOfNode)
			{
				if (entry.contains("$ref"))
				{
					const std::string ref = entry.at("$ref").get<std::string>();
					const nlohmann::json* refNode = ResolveDefsObjectNode(documentRoot, ref);

					if (!refNode)
					{
						diagnostics.Error(diag::schema::AllOfRef, "Could not resolve allOf object reference: " + ref);
						continue;
					}

					SchemaObject mergedObject = ParseObject(ResolveRefName(ref), false, *refNode, documentRoot, schemaPath, diagnostics);

					for (auto& field : mergedObject.fields)
					{
						MergeField(object, std::move(field), diagnostics);
					}

					continue;
				}

				if (entry.contains("if") || entry.contains("then") || entry.contains("else"))
				{
					diagnostics.Error(diag::schema::AllOfConditionalUnsupported, "Conditional allOf entries are not supported for codegen flattening.");
					continue;
				}

				if ((entry.contains("type") && entry.at("type") == "object") || entry.contains("properties"))
				{
					AppendObjectFields(object, entry, documentRoot, schemaPath, diagnostics);
					continue;
				}

				diagnostics.Error(diag::schema::AllOfEntryUnsupported, "Unsupported allOf entry in schema object '" + objectName + "'.");
			}

			if (node.contains("properties"))
			{
				AppendObjectFields(object, node, documentRoot, schemaPath, diagnostics);
			}

			if (isRootObject)
				MoveVersionFieldFirstIfPresent(object);

			return object;
		}

		if (!node.contains("type") || node.at("type") != "object")
		{
			diagnostics.Error(diag::schema::ObjectType, "Schema object '" + objectName + "' must have type: object.");
			return object;
		}

		AppendObjectFields(object, node, documentRoot, schemaPath, diagnostics);

		if (isRootObject)
			MoveVersionFieldFirstIfPresent(object);

		//object.additionalProperties = node.value("additionalProperties", true);

		return object;
	}

	void JsonSchemaParser::AppendObjectFields(
		SchemaObject& object,
		const nlohmann::json& node,
		const nlohmann::json& documentRoot,
		const std::filesystem::path& schemaPath,
		DiagnosticBag& diagnostics) const
	{
		(void)documentRoot;

		const auto requiredSet = ResolveRequiredSet(node);
		RecordRequiredFields(object, requiredSet);
		ReportUnsupportedKeywords(node, "object fields for '" + object.name + "'", diagnostics);

		if (!node.contains("properties") || !node.at("properties").is_object())
		{
			diagnostics.Error(diag::schema::Properties, "Schema object '" + object.name + "' must have properties.");
			return;
		}

		const auto& properties = node.at("properties");

		for (const auto& [jsonName, propNode] : properties.items())
		{
			const bool required = requiredSet.contains(jsonName);
			MergeField(object, ParseField(jsonName, propNode, documentRoot, schemaPath, required, diagnostics), diagnostics);
		}
	}

	void JsonSchemaParser::MergeField(
		SchemaObject& object,
		SchemaField field,
		DiagnosticBag& diagnostics) const
	{
		for (const auto& existingField : object.fields)
		{
			if (existingField.jsonName == field.jsonName)
			{
				RecordFieldConflict(object, field.jsonName);
				return;
			}
		}

		object.fields.push_back(std::move(field));
	}

	SchemaField JsonSchemaParser::ParseField(
		const std::string& jsonName, 
		const nlohmann::json& node, 
		const nlohmann::json& documentRoot,
		const std::filesystem::path& schemaPath,
		bool required,
		DiagnosticBag& diagnostics) const
	{
		SchemaField field;
		field.jsonName   = jsonName;
		field.cppName    = NameConverter::ToCppFieldName(jsonName);
		field.csharpName = NameConverter::ToCSharpPropertyName(jsonName);
		field.required   = required;

		if (node.contains("description"))
			field.description = node.at("description").get<std::string>();

		field.type = ParseType(node, documentRoot, schemaPath, diagnostics);

		return field;
	}

	std::unique_ptr<SchemaType> JsonSchemaParser::ParseType(
		const nlohmann::json& node,
		const nlohmann::json& documentRoot,
		const std::filesystem::path& schemaPath,
		DiagnosticBag& diagnostics) const
	{
		ReportUnsupportedKeywords(node, "type schema", diagnostics);

		if (node.contains(kCustomCppTypeKeyword))
		{
			const auto& cppTypeNode = node.at(kCustomCppTypeKeyword);
			if (!cppTypeNode.is_string())
			{
				diagnostics.Error(diag::schema::CustomCppTypeType, std::string(kCustomCppTypeKeyword) + " must be a string.");
				return std::make_unique<SchemaObjectRefType>("UnsupportedCustomType");
			}

			const std::string cppTypeName = cppTypeNode.get<std::string>();
			if (cppTypeName.empty())
			{
				diagnostics.Error(diag::schema::CustomCppTypeEmpty, std::string(kCustomCppTypeKeyword) + " must not be empty.");
				return std::make_unique<SchemaObjectRefType>("UnsupportedCustomType");
			}

			bool isHandleRef = false;
			if (node.contains(kCustomHandleRefKeyword))
			{
				const auto& handleRefNode = node.at(kCustomHandleRefKeyword);
				if (!handleRefNode.is_boolean())
				{
					diagnostics.Error(diag::schema::CustomHandleRefType, std::string(kCustomHandleRefKeyword) + " must be a boolean.");
					return std::make_unique<SchemaObjectRefType>("UnsupportedCustomType");
				}

				isHandleRef = handleRefNode.get<bool>();
			}

			return std::make_unique<SchemaCustomType>(
				cppTypeName,
				isHandleRef ? eSchemaCustomTypeKind::HandleRef : eSchemaCustomTypeKind::Opaque);
		}

		if (node.contains("oneOf"))
		{
			const bool isPolymorphic = node.value(kPolymorphicKeyword, false);

			if (!isPolymorphic)
			{
					diagnostics.Error(
					diag::schema::OneOfUnsupported,
					"oneOf is only supported for polymorphic object unions with x-jam-polymorphic.");
				return std::make_unique<SchemaObjectRefType>("UnsupportedOneOf");
			}

			if (!node.contains(kPolymorphicBaseNameKeyword) || !node.at(kPolymorphicBaseNameKeyword).is_string())
			{
					diagnostics.Error(
					diag::schema::OneOfBaseName,
					std::string(kPolymorphicBaseNameKeyword) + " must be a string for polymorphic oneOf.");
				return std::make_unique<SchemaObjectRefType>("UnsupportedOneOf");
			}

			if (!node.contains(kPolymorphicDiscriminatorKeyword) || !node.at(kPolymorphicDiscriminatorKeyword).is_string())
			{
					diagnostics.Error(
					diag::schema::OneOfDiscriminator,
					std::string(kPolymorphicDiscriminatorKeyword) + " must be a string for polymorphic oneOf.");
				return std::make_unique<SchemaObjectRefType>("UnsupportedOneOf");
			}

			const auto& oneOfNode = node.at("oneOf");

			if (!oneOfNode.is_array())
			{
				diagnostics.Error(diag::schema::OneOfArray, "Polymorphic oneOf must be an array.");
				return std::make_unique<SchemaObjectRefType>("UnsupportedOneOf");
			}

			if (node.contains("type"))
			{
				if (!node.at("type").is_string() || node.at("type") != "object")
				{
					diagnostics.Error(
						diag::schema::OneOfType,
						"Polymorphic oneOf may only use type: object.");
				}
			}

			const std::string baseTypeName = NameConverter::ToGeneratedDtoTypeName(node.at(kPolymorphicBaseNameKeyword).get<std::string>());
			const std::string discriminatorField = node.at(kPolymorphicDiscriminatorKeyword).get<std::string>();
			std::vector<SchemaPolymorphicBranch> branches;

			for (const auto& branchEntry : oneOfNode)
			{
				if (!branchEntry.contains("$ref") || !branchEntry.at("$ref").is_string())
				{
					diagnostics.Error(diag::schema::OneOfBranchRef, "Polymorphic oneOf branch must be a $ref to an object definition.");
					continue;
				}

				const std::string ref = branchEntry.at("$ref").get<std::string>();
				const nlohmann::json* branchNode = ResolveDefsObjectNode(documentRoot, ref);

				if (!branchNode)
				{
					diagnostics.Error(diag::schema::OneOfBranchResolve, "Could not resolve polymorphic branch reference: " + ref);
					continue;
				}

				const std::string discriminatorValue =
					ResolvePolymorphicDiscriminatorValue(*branchNode, discriminatorField, diagnostics);

				if (discriminatorValue.empty())
					continue;

				branches.push_back(SchemaPolymorphicBranch{ NameConverter::ToGeneratedDtoTypeName(ResolveRefName(ref)), discriminatorValue });
			}

			return std::make_unique<SchemaPolymorphicType>(baseTypeName, discriminatorField, std::move(branches));
		}

		if (node.contains("enum"))
		{
			if (!node.contains("type") || node.at("type") != "string")
			{
				diagnostics.Error(diag::schema::EnumType, "Only string enum is supported. Enum field must have type: string.");
			}

			const auto& enumNode = node.at("enum");

			if (!enumNode.is_array())
			{
				diagnostics.Error(diag::schema::EnumArray, "Enum must be an array.");
				return std::make_unique<SchemaPrimitiveType>(eSchemaPrimitiveKind::String);
			}

			std::vector<std::string> values;

			for (const auto& item : enumNode)
			{
				if (!item.is_string())
				{
					diagnostics.Error(diag::schema::EnumValue, "Only string enum values are supported.");
					continue;
				}

				values.push_back(item.get<std::string>());
			}

			return std::make_unique<SchemaEnumType>(std::move(values));
		}


		if (node.contains("$ref"))
		{
			std::string ref = node.at("$ref").get<std::string>();

			if (const nlohmann::json* refNode = ResolveDefsObjectNode(documentRoot, ref))
			{
				if (IsNamedObjectSchema(*refNode))
				{
					return std::make_unique<SchemaObjectRefType>(NameConverter::ToGeneratedDtoTypeName(ResolveRefName(ref)));
				}

				return ParseType(*refNode, documentRoot, schemaPath, diagnostics);
			}

			diagnostics.Error(diag::schema::RefResolve, "Could not resolve $ref target: " + ref);
			return std::make_unique<SchemaObjectRefType>(NameConverter::ToGeneratedDtoTypeName(ResolveRefName(ref)));
		}

		if (!node.contains("type"))
		{
			diagnostics.Error(diag::schema::MissingType, "Schema node is missing 'type'.");
			return std::make_unique<SchemaPrimitiveType>(eSchemaPrimitiveKind::String);
		}

		const std::string type = node.at("type").get<std::string>();

		if (type == "string")
		{
			return std::make_unique<SchemaPrimitiveType>(eSchemaPrimitiveKind::String, ResolveScalarHint(node, eSchemaPrimitiveKind::String, diagnostics));
		}

		if (type == "integer")
		{
			return std::make_unique<SchemaPrimitiveType>(eSchemaPrimitiveKind::Integer, ResolveScalarHint(node, eSchemaPrimitiveKind::Integer, diagnostics));
		}

		if (type == "number")
		{
			return std::make_unique<SchemaPrimitiveType>(eSchemaPrimitiveKind::Number, ResolveScalarHint(node, eSchemaPrimitiveKind::Number, diagnostics));
		}

		if (type == "boolean")
		{
			return std::make_unique<SchemaPrimitiveType>(eSchemaPrimitiveKind::Boolean, ResolveScalarHint(node, eSchemaPrimitiveKind::Boolean, diagnostics));
		}

		if (type == "array")
		{
			if (!node.contains("items"))
			{
				diagnostics.Error(diag::schema::ArrayItems, "Array schema is missing 'items'.");
				return std::make_unique<SchemaArrayType>(std::make_unique<SchemaPrimitiveType>(eSchemaPrimitiveKind::String));
			}

			return std::make_unique<SchemaArrayType>(ParseType(node.at("items"), documentRoot, schemaPath, diagnostics));
		}

		if (type == "object")
		{
			if (node.contains("properties"))
			{
				diagnostics.Error(diag::schema::InlineObject, "Inline object is not supported yet. Use $defs and $ref.");
				return std::make_unique<SchemaObjectRefType>("UnsupportedObject");
			}

			if (node.contains("additionalProperties"))
			{
				const auto& additionalPropertiesNode = node.at("additionalProperties");

				if (!additionalPropertiesNode.is_object())
				{
					diagnostics.Error(diag::schema::MapAdditionalProperties, "Map schema must define additionalProperties as a schema object.");
					return std::make_unique<SchemaObjectRefType>("UnsupportedObject");
				}

				return std::make_unique<SchemaMapType>(ParseType(additionalPropertiesNode, documentRoot, schemaPath, diagnostics));
			}

			diagnostics.Error(diag::schema::ObjectShape, "Unsupported object schema. Field-level object must use properties or additionalProperties.");
			return std::make_unique<SchemaObjectRefType>("UnsupportedObject");
		}

		diagnostics.Error(diag::schema::UnknownType, "Unsupported schema type: " + type);

		return std::make_unique<SchemaPrimitiveType>(eSchemaPrimitiveKind::String);
	}

}
