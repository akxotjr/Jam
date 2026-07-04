#include "Emit/CppEmitter.h"
#include "Emit/CppTypeMapper.h"
#include "IO/FileIO.h"
#include "Naming/NameConverter.h"

#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace jam::tool
{
	namespace
	{
		void CollectObjectDependenciesFromType(const SchemaType& type, std::unordered_set<std::string>& outDependencies)
		{
			switch (type.kind)
			{
			case eSchemaTypeKind::ObjectRef:
				outDependencies.insert(static_cast<const SchemaObjectRefType&>(type).targetName);
				return;

			case eSchemaTypeKind::Array:
				CollectObjectDependenciesFromType(*static_cast<const SchemaArrayType&>(type).elementType, outDependencies);
				return;

			case eSchemaTypeKind::Map:
				CollectObjectDependenciesFromType(*static_cast<const SchemaMapType&>(type).valueType, outDependencies);
				return;

			case eSchemaTypeKind::Polymorphic:
			{
				const auto& polymorphicType = static_cast<const SchemaPolymorphicType&>(type);
				for (const auto& branch : polymorphicType.branches)
					outDependencies.insert(branch.objectTypeName);
				return;
			}

			default:
				return;
			}
		}

		std::unordered_set<std::string> CollectObjectDependencies(const SchemaObject& object)
		{
			std::unordered_set<std::string> dependencies;

			for (const auto& field : object.fields)
				CollectObjectDependenciesFromType(*field.type, dependencies);

			dependencies.erase(object.name);
			return dependencies;
		}

		void VisitObjectDependencyOrder(
			const SchemaObject& object,
			const std::unordered_map<std::string, const SchemaObject*>& objectsByName,
			std::unordered_set<std::string>& visiting,
			std::unordered_set<std::string>& visited,
			std::vector<const SchemaObject*>& ordered)
		{
			if (visited.contains(object.name))
				return;
			if (!visiting.insert(object.name).second)
				return;

			for (const auto& dependencyName : CollectObjectDependencies(object))
			{
				if (const auto it = objectsByName.find(dependencyName); it != objectsByName.end())
					VisitObjectDependencyOrder(*it->second, objectsByName, visiting, visited, ordered);
			}

			visiting.erase(object.name);
			visited.insert(object.name);
			ordered.push_back(&object);
		}

		std::vector<const SchemaObject*> OrderObjectsForCppEmission(const SchemaDocument& document)
		{
			std::unordered_map<std::string, const SchemaObject*> objectsByName;
			objectsByName.reserve(document.objects.size());

			for (const auto& object : document.objects)
				objectsByName.emplace(object.name, &object);

			std::vector<const SchemaObject*> ordered;
			ordered.reserve(document.objects.size());

			std::unordered_set<std::string> visiting;
			std::unordered_set<std::string> visited;

			for (const auto& object : document.objects)
				VisitObjectDependencyOrder(object, objectsByName, visiting, visited, ordered);

			return ordered;
		}

		const SchemaEnumType* FindNestedEnumType(const SchemaType& type)
		{
			switch (type.kind)
			{
			case eSchemaTypeKind::Enum:
				return &static_cast<const SchemaEnumType&>(type);

			case eSchemaTypeKind::Array:
				return FindNestedEnumType(*static_cast<const SchemaArrayType&>(type).elementType);

			case eSchemaTypeKind::Map:
				return FindNestedEnumType(*static_cast<const SchemaMapType&>(type).valueType);

			default:
				return nullptr;
			}
		}

		bool TypeContainsCustomHandleRef(const SchemaType& type)
		{
			switch (type.kind)
			{
			case eSchemaTypeKind::Array:
				return TypeContainsCustomHandleRef(*static_cast<const SchemaArrayType&>(type).elementType);

			case eSchemaTypeKind::Map:
				return TypeContainsCustomHandleRef(*static_cast<const SchemaMapType&>(type).valueType);

			case eSchemaTypeKind::Custom:
				return static_cast<const SchemaCustomType&>(type).customKind == eSchemaCustomTypeKind::HandleRef;

			default:
				return false;
			}
		}

		const SchemaPolymorphicType* TryGetDirectArrayPolymorphic(const SchemaType& type)
		{
			if (type.kind != eSchemaTypeKind::Array)
				return nullptr;

			const auto& arrayType = static_cast<const SchemaArrayType&>(type);
			if (arrayType.elementType->kind != eSchemaTypeKind::Polymorphic)
				return nullptr;

			return &static_cast<const SchemaPolymorphicType&>(*arrayType.elementType);
		}

		const SchemaPolymorphicType* TryGetDirectMapPolymorphic(const SchemaType& type)
		{
			if (type.kind != eSchemaTypeKind::Map)
				return nullptr;

			const auto& mapType = static_cast<const SchemaMapType&>(type);
			if (mapType.valueType->kind != eSchemaTypeKind::Polymorphic)
				return nullptr;

			return &static_cast<const SchemaPolymorphicType&>(*mapType.valueType);
		}

		bool DocumentContainsCustomHandleRef(const SchemaDocument& document)
		{
			for (const auto& object : document.objects)
			{
				for (const auto& field : object.fields)
				{
					if (TypeContainsCustomHandleRef(*field.type))
						return true;
				}
			}

			return false;
		}

		void CollectPolymorphicTypesFromType(
			const SchemaType& type,
			std::vector<const SchemaPolymorphicType*>& outTypes,
			std::unordered_set<std::string>& seenBaseNames)
		{
			switch (type.kind)
			{
			case eSchemaTypeKind::Array:
			{
				const auto& arrayType = static_cast<const SchemaArrayType&>(type);
				CollectPolymorphicTypesFromType(*arrayType.elementType, outTypes, seenBaseNames);
				return;
			}

			case eSchemaTypeKind::Map:
			{
				const auto& mapType = static_cast<const SchemaMapType&>(type);
				CollectPolymorphicTypesFromType(*mapType.valueType, outTypes, seenBaseNames);
				return;
			}

			case eSchemaTypeKind::Polymorphic:
			{
				const auto& polymorphicType = static_cast<const SchemaPolymorphicType&>(type);

				if (seenBaseNames.insert(polymorphicType.baseTypeName).second)
					outTypes.push_back(&polymorphicType);

				return;
			}

			default:
				return;
			}
		}

		std::vector<const SchemaPolymorphicType*> CollectPolymorphicTypes(const SchemaDocument& document)
		{
			std::vector<const SchemaPolymorphicType*> polymorphicTypes;
			std::unordered_set<std::string> seenBaseNames;

			for (const auto& object : document.objects)
			{
				for (const auto& field : object.fields)
				{
					CollectPolymorphicTypesFromType(*field.type, polymorphicTypes, seenBaseNames);
				}
			}

			return polymorphicTypes;
		}

		std::string FindBaseTypeForObject(const SchemaDocument& document, const std::string& objectTypeName)
		{
			for (const auto* polymorphicType : CollectPolymorphicTypes(document))
			{
				for (const auto& branch : polymorphicType->branches)
				{
					if (branch.objectTypeName == objectTypeName)
						return polymorphicType->baseTypeName;
				}
			}

			return {};
		}

		std::string EmitPolymorphicHelperDeclaration(const SchemaPolymorphicType& polymorphicType)
		{
			std::ostringstream oss;
			oss << "    template<>\n";
			oss << "    std::unique_ptr<" << polymorphicType.baseTypeName << "> ToPolymorphic<" << polymorphicType.baseTypeName << ">(const nlohmann::json& j);\n";
			oss << "    template<>\n";
			oss << "    nlohmann::json FromPolymorphic<" << polymorphicType.baseTypeName << ">(const std::unique_ptr<" << polymorphicType.baseTypeName << ">& value);\n";
			return oss.str();
		}

		std::string EmitHandleRefHelpers()
		{
			std::ostringstream oss;
			oss << "    template<class THandle>\n";
			oss << "    inline THandle DeserializeHandleRef(const nlohmann::json& j)\n";
			oss << "    {\n";
			oss << "        if (j.is_number_unsigned() || j.is_number_integer())\n";
			oss << "            return THandle::FromU64(j.get<uint64_t>());\n\n";
			oss << "        if (j.is_string())\n";
			oss << "            return THandle::FromU64(jam::fnv1a<uint64_t>(j.get<std::string>()));\n\n";
			oss << "        throw std::runtime_error(\"handle_ref must be string or u64\");\n";
			oss << "    }\n\n";
			oss << "    template<class THandle>\n";
			oss << "    inline void SerializeHandleRef(nlohmann::json& j, const THandle& value)\n";
			oss << "    {\n";
			oss << "        j = value.value();\n";
			oss << "    }\n";
			return oss.str();
		}
	}

	void CppEmitter::Emit(const SchemaDocument& document, const EmitOptions& options, DiagnosticBag& diagnostics)
	{
		const std::string text = EmitHeader(document, options);
		const auto outputPath = options.outputDirectory / MakeOutputFileName(document);

		try
		{
			FileIO::WriteAllText(outputPath, text);
		}
		catch (const std::exception& e)
		{
			diagnostics.Error("CPP_EMIT_FAILED", "Failed to emit C++ file: " + outputPath.string() + "\n" + std::string(e.what()));
		}
	}

	std::string CppEmitter::EmitHeader(const SchemaDocument& document, const EmitOptions& options) const
	{
		std::ostringstream oss;
		const auto orderedObjects = OrderObjectsForCppEmission(document);

		oss << "// This file is generated by jam_shared_data_tool.\n";
		oss << "// Do not modify this file manually.\n\n";

		oss << "#pragma once\n\n";

		oss << "#include <cstdint>\n";
		oss << "#include <filesystem>\n";
		oss << "#include <fstream>\n";
		oss << "#include <jambase/Fnv1a.h>\n";
		oss << "#include <memory>\n";
		oss << "#include <stdexcept>\n";
		oss << "#include <unordered_map>\n";
		oss << "#include <string>\n";
		oss << "#include <vector>\n";
		oss << "#include <nlohmann/json.hpp>\n\n";

		oss << "namespace " << options.cppNamespace << "\n";
		oss << "{\n";

		if (!CollectPolymorphicTypes(document).empty())
		{
			oss << "    template<typename TBase>\n";
			oss << "    std::unique_ptr<TBase> ToPolymorphic(const nlohmann::json& j);\n\n";
			oss << "    template<typename TBase>\n";
			oss << "    nlohmann::json FromPolymorphic(const std::unique_ptr<TBase>& value);\n\n";
		}

		for (const auto* object : orderedObjects)
		{
			oss << "    struct " << object->name << ";\n";
		}
		oss << "\n";

		oss << EmitPolymorphicBases(document);

		if (DocumentContainsCustomHandleRef(document))
		{
			oss << EmitHandleRefHelpers();
			oss << "\n";
		}

		for (const auto* object : orderedObjects)
		{
			oss << EmitEnums(*object);
		}

		for (const auto* object : orderedObjects)
		{
			oss << EmitObject(document, *object);
			oss << "\n";
		}

		for (const auto* polymorphicType : CollectPolymorphicTypes(document))
		{
			oss << EmitPolymorphicHelperDeclaration(*polymorphicType);
		}
		oss << "\n";

		for (const auto* object : orderedObjects)
		{
			for (const auto& field : object->fields)
			{
				const auto* enumType = FindNestedEnumType(*field.type);
				if (!enumType)
					continue;

				const std::string enumTypeName = CppTypeMapper::MakeFieldLocalEnumTypeName(*object, field);
				oss << "    inline void from_json(const nlohmann::json& j, " << enumTypeName << "& v);\n";
				oss << "    inline void to_json(nlohmann::json& j, const " << enumTypeName << "& v);\n";
			}
		}

		for (const auto* object : orderedObjects)
		{
			oss << "    inline void from_json(const nlohmann::json& j, " << object->name << "& v);\n";
			oss << "    inline void to_json(nlohmann::json& j, const " << object->name << "& v);\n";
		}
		oss << "\n";

		for (const auto* object : orderedObjects)
		{
			oss << EmitEnumSerializers(*object);
		}

		for (const auto* object : orderedObjects)
		{
			oss << EmitFromJson(*object);
			oss << "\n";
			oss << EmitToJson(*object);
			oss << "\n";
		}

		oss << EmitPolymorphicHelpers(document);
		oss << EmitRootJsonHelpers(document);

		oss << "} // namespace " << options.cppNamespace << "\n";

		return oss.str();
	}

	std::string CppEmitter::EmitPolymorphicBases(const SchemaDocument& document) const
	{
		std::ostringstream oss;

		for (const auto* polymorphicType : CollectPolymorphicTypes(document))
		{
			oss << EmitPolymorphicBase(*polymorphicType);
			oss << "\n";
		}

		return oss.str();
	}

	std::string CppEmitter::EmitPolymorphicBase(const SchemaPolymorphicType& polymorphicType) const
	{
		std::ostringstream oss;
		oss << "    struct " << polymorphicType.baseTypeName << "\n";
		oss << "    {\n";
		oss << "        virtual ~" << polymorphicType.baseTypeName << "() = default;\n";
		oss << "    };\n";
		return oss.str();
	}

	std::string CppEmitter::EmitPolymorphicHelpers(const SchemaDocument& document) const
	{
		std::ostringstream oss;

		for (const auto* polymorphicType : CollectPolymorphicTypes(document))
		{
			oss << EmitPolymorphicHelper(*polymorphicType);
			oss << "\n";
		}

		return oss.str();
	}

	std::string CppEmitter::EmitPolymorphicHelper(const SchemaPolymorphicType& polymorphicType) const
	{
		std::ostringstream oss;

		oss << "    template<>\n";
		oss << "    inline std::unique_ptr<" << polymorphicType.baseTypeName << "> ToPolymorphic<" << polymorphicType.baseTypeName << ">(const nlohmann::json& j)\n";
		oss << "    {\n";
		oss << "        const std::string discriminator = j.at(\"" << polymorphicType.discriminatorField << "\").get<std::string>();\n\n";

		for (const auto& branch : polymorphicType.branches)
		{
			oss << "        if (discriminator == \"" << branch.discriminatorValue << "\")\n";
			oss << "            return std::make_unique<" << branch.objectTypeName << ">(j.get<" << branch.objectTypeName << ">());\n";
		}

		oss << "\n";
		oss << "        throw std::runtime_error(\"Unknown discriminator for " << polymorphicType.baseTypeName << ": \" + discriminator);\n";
		oss << "    }\n\n";

		oss << "    template<>\n";
		oss << "    inline nlohmann::json FromPolymorphic<" << polymorphicType.baseTypeName << ">(const std::unique_ptr<" << polymorphicType.baseTypeName << ">& value)\n";
		oss << "    {\n";
		oss << "        if (!value)\n";
		oss << "            return nullptr;\n\n";

		for (const auto& branch : polymorphicType.branches)
		{
			oss << "        if (const auto* branchValue = dynamic_cast<const " << branch.objectTypeName << "*>(value.get()))\n";
			oss << "            return nlohmann::json(*branchValue);\n";
		}

		oss << "\n";
		oss << "        throw std::runtime_error(\"Unknown runtime type for " << polymorphicType.baseTypeName << ".\");\n";
		oss << "    }\n";

		return oss.str();
	}

	std::string CppEmitter::EmitEnums(const SchemaObject& object) const
	{
		std::ostringstream oss;

		for (const auto& field : object.fields)
		{
			if (!FindNestedEnumType(*field.type))
				continue;

			oss << EmitEnumDeclaration(object, field);
			oss << "\n";
		}

		return oss.str();
	}

	std::string CppEmitter::EmitEnumSerializers(const SchemaObject& object) const
	{
		std::ostringstream oss;

		for (const auto& field : object.fields)
		{
			const auto* enumType = FindNestedEnumType(*field.type);
			if (!enumType)
				continue;

			oss << EmitEnumFromJson(object, field, *enumType) << "\n";
			oss << EmitEnumToJson(object, field, *enumType) << "\n\n";
		}

		return oss.str();
	}

	std::string CppEmitter::EmitEnumDeclaration(const SchemaObject& object, const SchemaField& field) const
	{
		const auto* enumType = FindNestedEnumType(*field.type);
		if (!enumType)
			return {};

		const std::string enumTypeName = CppTypeMapper::MakeFieldLocalEnumTypeName(object, field);
		std::ostringstream oss;

		oss << "    enum class " << enumTypeName << "\n";
		oss << "    {\n";

		for (std::size_t i = 0; i < enumType->values.size(); ++i)
		{
			oss << "        " << NameConverter::ToCppEnumMemberName(enumType->values[i]);

			if (i + 1 < enumType->values.size())
				oss << ",";

			oss << "\n";
		}

		oss << "    };\n";

		return oss.str();
	}

	std::string CppEmitter::EmitObject(const SchemaDocument& document, const SchemaObject& object) const
	{
		std::ostringstream oss;
		const std::string baseTypeName = FindBaseTypeForObject(document, object.name);

		oss << "    struct " << object.name;

		if (!baseTypeName.empty())
			oss << " : " << baseTypeName;

		oss << "\n";
		oss << "    {\n";

		for (const auto& field : object.fields)
		{
			oss << "        " << CppTypeMapper::MapFieldType(object, field) << " " << field.cppName << " = {};\n";
		}

		oss << "    };\n";

		return oss.str();
	}

	std::string CppEmitter::EmitEnumFromJson(const SchemaObject& object, const SchemaField& field, const SchemaEnumType& enumType) const
	{
		const std::string enumTypeName = CppTypeMapper::MakeFieldLocalEnumTypeName(object, field);
		std::ostringstream oss;

		oss << "    inline void from_json(const nlohmann::json& j, " << enumTypeName << "& v)\n";
		oss << "    {\n";
		oss << "        const std::string s = j.get<std::string>();\n\n";

		for (const auto& value : enumType.values)
		{
			oss << "        if (s == \"" << value << "\") { v = " << enumTypeName << "::" << NameConverter::ToCppEnumMemberName(value) << "; return; }\n";
		}

		oss << "\n";
		oss << "        throw std::runtime_error(\"Unknown enum value for " << enumTypeName << ": \" + s);\n";
		oss << "    }\n";

		return oss.str();
	}

	std::string CppEmitter::EmitEnumToJson(const SchemaObject& object, const SchemaField& field, const SchemaEnumType& enumType) const
	{
		const std::string enumTypeName = CppTypeMapper::MakeFieldLocalEnumTypeName(object, field);
		std::ostringstream oss;

		oss << "    inline void to_json(nlohmann::json& j, const " << enumTypeName << "& v)\n";
		oss << "    {\n";
		oss << "        switch (v)\n";
		oss << "        {\n";

		for (const auto& value : enumType.values)
		{
			oss << "        case " << enumTypeName << "::" << NameConverter::ToCppEnumMemberName(value) << ": j = \"" << value << "\"; return;\n";
		}

		oss << "        }\n\n";
		oss << "        throw std::runtime_error(\"Unknown " << enumTypeName << " enum state.\");\n";
		oss << "    }\n";

		return oss.str();
	}

	std::string CppEmitter::EmitFromJson(const SchemaObject& object) const
	{
		std::ostringstream oss;

		oss << "    inline void from_json(const nlohmann::json& j, " << object.name << "& v)\n";
		oss << "    {\n";

		for (const auto& field : object.fields)
		{
			if (field.type->kind == eSchemaTypeKind::Polymorphic)
			{
				const auto& polymorphicType = static_cast<const SchemaPolymorphicType&>(*field.type);

				if (field.required)
				{
					oss << "        v." << field.cppName << " = ToPolymorphic<" << polymorphicType.baseTypeName << ">(j.at(\"" << field.jsonName << "\"));\n";
				}
				else
				{
					oss << "        if (j.contains(\"" << field.jsonName << "\"))\n";
					oss << "            v." << field.cppName << " = ToPolymorphic<" << polymorphicType.baseTypeName << ">(j.at(\"" << field.jsonName << "\"));\n";
				}

				continue;
			}

			if (const SchemaPolymorphicType* polymorphicType = TryGetDirectArrayPolymorphic(*field.type))
			{
				if (field.required)
				{
					oss << "        v." << field.cppName << ".clear();\n";
					oss << "        for (const auto& item : j.at(\"" << field.jsonName << "\"))\n";
					oss << "            v." << field.cppName << ".push_back(ToPolymorphic<" << polymorphicType->baseTypeName << ">(item));\n";
				}
				else
				{
					oss << "        if (j.contains(\"" << field.jsonName << "\"))\n";
					oss << "        {\n";
					oss << "            v." << field.cppName << ".clear();\n";
					oss << "            for (const auto& item : j.at(\"" << field.jsonName << "\"))\n";
					oss << "                v." << field.cppName << ".push_back(ToPolymorphic<" << polymorphicType->baseTypeName << ">(item));\n";
					oss << "        }\n";
				}

				continue;
			}

			if (const SchemaPolymorphicType* polymorphicType = TryGetDirectMapPolymorphic(*field.type))
			{
				if (field.required)
				{
					oss << "        v." << field.cppName << ".clear();\n";
					oss << "        for (const auto& [key, item] : j.at(\"" << field.jsonName << "\").items())\n";
					oss << "            v." << field.cppName << "[key] = ToPolymorphic<" << polymorphicType->baseTypeName << ">(item);\n";
				}
				else
				{
					oss << "        if (j.contains(\"" << field.jsonName << "\"))\n";
					oss << "        {\n";
					oss << "            v." << field.cppName << ".clear();\n";
					oss << "            for (const auto& [key, item] : j.at(\"" << field.jsonName << "\").items())\n";
					oss << "                v." << field.cppName << "[key] = ToPolymorphic<" << polymorphicType->baseTypeName << ">(item);\n";
					oss << "        }\n";
				}

				continue;
			}

			if (field.type->kind == eSchemaTypeKind::Custom)
			{
				const auto& customType = static_cast<const SchemaCustomType&>(*field.type);
				if (customType.customKind == eSchemaCustomTypeKind::HandleRef)
				{
					if (field.required)
					{
						oss << "        v." << field.cppName << " = DeserializeHandleRef<" << customType.cppTypeName << ">(j.at(\"" << field.jsonName << "\"));\n";
					}
					else
					{
						oss << "        if (j.contains(\"" << field.jsonName << "\"))\n";
						oss << "            v." << field.cppName << " = DeserializeHandleRef<" << customType.cppTypeName << ">(j.at(\"" << field.jsonName << "\"));\n";
					}

					continue;
				}
			}

			if (field.required)
			{
				oss << "        j.at(\"" << field.jsonName << "\").get_to(v." << field.cppName << ");\n";
			}
			else
			{
				oss << "        if (j.contains(\"" << field.jsonName << "\"))\n";
				oss << "            j.at(\"" << field.jsonName << "\").get_to(v." << field.cppName << ");\n";
			}
		}

		oss << "    }\n";

		return oss.str();
	}

	std::string CppEmitter::EmitToJson(const SchemaObject& object) const
	{
		std::ostringstream oss;

		oss << "    inline void to_json(nlohmann::json& j, const " << object.name << "& v)\n";
		oss << "    {\n";
		oss << "        j = nlohmann::json::object();\n";

		for (const auto& field : object.fields)
		{
			if (field.type->kind == eSchemaTypeKind::Polymorphic)
			{
				const auto& polymorphicType = static_cast<const SchemaPolymorphicType&>(*field.type);
				oss << "        if (v." << field.cppName << ")\n";
				oss << "            j[\"" << field.jsonName << "\"] = FromPolymorphic<" << polymorphicType.baseTypeName << ">(v." << field.cppName << ");\n";
				continue;
			}

			if (const SchemaPolymorphicType* polymorphicType = TryGetDirectArrayPolymorphic(*field.type))
			{
				oss << "        j[\"" << field.jsonName << "\"] = nlohmann::json::array();\n";
				oss << "        for (const auto& item : v." << field.cppName << ")\n";
				oss << "            j[\"" << field.jsonName << "\"].push_back(item ? FromPolymorphic<" << polymorphicType->baseTypeName << ">(item) : nlohmann::json(nullptr));\n";
				continue;
			}

			if (const SchemaPolymorphicType* polymorphicType = TryGetDirectMapPolymorphic(*field.type))
			{
				oss << "        j[\"" << field.jsonName << "\"] = nlohmann::json::object();\n";
				oss << "        for (const auto& [key, item] : v." << field.cppName << ")\n";
				oss << "            j[\"" << field.jsonName << "\"][key] = item ? FromPolymorphic<" << polymorphicType->baseTypeName << ">(item) : nlohmann::json(nullptr);\n";
				continue;
			}

			if (field.type->kind == eSchemaTypeKind::Custom)
			{
				const auto& customType = static_cast<const SchemaCustomType&>(*field.type);
				if (customType.customKind == eSchemaCustomTypeKind::HandleRef)
				{
					oss << "        SerializeHandleRef(j[\"" << field.jsonName << "\"], v." << field.cppName << ");\n";
					continue;
				}
			}

			oss << "        j[\"" << field.jsonName << "\"] = v." << field.cppName << ";\n";
		}

		oss << "    }\n";

		return oss.str();
	}

	std::string CppEmitter::EmitRootJsonHelpers(const SchemaDocument& document) const
	{
		if (document.objects.empty())
			return {};

		const SchemaObject& rootObject = document.objects.front();
		const std::string rootTypeName = rootObject.name;
		std::ostringstream oss;

		oss << "\n";
		oss << "    inline " << rootTypeName << " Deserialize" << rootTypeName << "(const nlohmann::json& j)\n";
		oss << "    {\n";
		oss << "        return j.get<" << rootTypeName << ">();\n";
		oss << "    }\n\n";
		oss << "    inline nlohmann::json Serialize" << rootTypeName << "(const " << rootTypeName << "& v)\n";
		oss << "    {\n";
		oss << "        return nlohmann::json(v);\n";
		oss << "    }\n\n";
		oss << "    inline " << rootTypeName << " Load" << rootTypeName << "(const std::filesystem::path& path)\n";
		oss << "    {\n";
		oss << "        std::ifstream stream(path);\n";
		oss << "        if (!stream.is_open())\n";
		oss << "            throw std::runtime_error(\"Failed to open json file for read: \" + path.string());\n\n";
		oss << "        nlohmann::json j;\n";
		oss << "        stream >> j;\n";
		oss << "        return Deserialize" << rootTypeName << "(j);\n";
		oss << "    }\n\n";
		oss << "    inline void Save" << rootTypeName << "(const std::filesystem::path& path, const " << rootTypeName << "& v)\n";
		oss << "    {\n";
		oss << "        std::ofstream stream(path);\n";
		oss << "        if (!stream.is_open())\n";
		oss << "            throw std::runtime_error(\"Failed to open json file for write: \" + path.string());\n\n";
		oss << "        stream << Serialize" << rootTypeName << "(v).dump(4);\n";
		oss << "    }\n";

		return oss.str();
	}

	std::string CppEmitter::MakeOutputFileName(const SchemaDocument& document) const
	{
		const auto stem = document.srcPath.stem().string();

		if (stem.ends_with(".schema"))
			return stem.substr(0, stem.size() - 7) + ".generated.hpp";

		return stem + ".generated.hpp";
	}
}
