#include "Inspect/SchemaAstDumper.h"

#include "AST/SchemaField.h"
#include "AST/SchemaObject.h"
#include "AST/SchemaType.h"

#include <sstream>

namespace jam::tool
{
	namespace
	{
		const char* PrimitiveKindToString(eSchemaPrimitiveKind primitiveKind)
		{
			switch (primitiveKind)
			{
			case eSchemaPrimitiveKind::String:
				return "string";
			case eSchemaPrimitiveKind::Integer:
				return "integer";
			case eSchemaPrimitiveKind::Number:
				return "number";
			case eSchemaPrimitiveKind::Boolean:
				return "boolean";
			}

			return "unknown-primitive";
		}

		const char* ScalarHintToString(eSchemaScalarHint scalarHint)
		{
			switch (scalarHint)
			{
			case eSchemaScalarHint::None:
				return "none";
			case eSchemaScalarHint::F32:
				return "f32";
			case eSchemaScalarHint::F64:
				return "f64";
			case eSchemaScalarHint::I32:
				return "i32";
			case eSchemaScalarHint::I64:
				return "i64";
			case eSchemaScalarHint::U32:
				return "u32";
			case eSchemaScalarHint::U64:
				return "u64";
			}

			return "unknown-scalar-hint";
		}

		void AppendIndent(std::ostringstream& stream, int indentLevel)
		{
			for (int i = 0; i < indentLevel; ++i)
			{
				stream << "  ";
			}
		}

		void AppendType(std::ostringstream& stream, const SchemaType& type, int indentLevel)
		{
			switch (type.kind)
			{
			case eSchemaTypeKind::Primitive:
			{
				const auto& primitiveType = static_cast<const SchemaPrimitiveType&>(type);

				stream << "primitive(" << PrimitiveKindToString(primitiveType.primitive);

				if (primitiveType.scalarHint != eSchemaScalarHint::None)
					stream << ", scalar=" << ScalarHintToString(primitiveType.scalarHint);

				stream << ")";
				return;
			}

			case eSchemaTypeKind::Array:
			{
				const auto& arrayType = static_cast<const SchemaArrayType&>(type);
				stream << "array<";
				AppendType(stream, *arrayType.elementType, indentLevel);
				stream << ">";
				return;
			}

			case eSchemaTypeKind::ObjectRef:
			{
				const auto& objectRefType = static_cast<const SchemaObjectRefType&>(type);
				stream << "object-ref(" << objectRefType.targetName << ")";
				return;
			}

			case eSchemaTypeKind::Enum:
			{
				const auto& enumType = static_cast<const SchemaEnumType&>(type);
				stream << "enum[";

				for (std::size_t i = 0; i < enumType.values.size(); ++i)
				{
					stream << "\"" << enumType.values[i] << "\"";

					if (i + 1 < enumType.values.size())
						stream << ", ";
				}

				stream << "]";
				return;
			}

			case eSchemaTypeKind::Map:
			{
				const auto& mapType = static_cast<const SchemaMapType&>(type);
				stream << "map<string, ";
				AppendType(stream, *mapType.valueType, indentLevel);
				stream << ">";
				return;
			}

			case eSchemaTypeKind::Polymorphic:
			{
				const auto& polymorphicType = static_cast<const SchemaPolymorphicType&>(type);
				stream << "polymorphic(base=" << polymorphicType.baseTypeName
					<< ", discriminator=" << polymorphicType.discriminatorField
					<< ", branches=[";

				for (std::size_t i = 0; i < polymorphicType.branches.size(); ++i)
				{
					const auto& branch = polymorphicType.branches[i];
					stream << branch.objectTypeName << ":" << branch.discriminatorValue;

					if (i + 1 < polymorphicType.branches.size())
						stream << ", ";
				}

				stream << "])";
				return;
			}

			case eSchemaTypeKind::Custom:
			{
				const auto& customType = static_cast<const SchemaCustomType&>(type);
				stream << "custom(cpp=" << customType.cppTypeName
					<< ", kind=" << (customType.customKind == eSchemaCustomTypeKind::HandleRef ? "handle_ref" : "opaque")
					<< ")";
				return;
			}

			}
			stream << "unknown";
		}

		void AppendField(std::ostringstream& stream, const SchemaField& field, int indentLevel)
		{
			AppendIndent(stream, indentLevel);
			stream
				<< "- field json=\"" << field.jsonName
				<< "\" cpp=\"" << field.cppName
				<< "\" csharp=\"" << field.csharpName
				<< "\" required=" << (field.required ? "true" : "false")
				<< " type=";
			AppendType(stream, *field.type, indentLevel + 1);
			stream << "\n";

			if (!field.description.empty())
			{
				AppendIndent(stream, indentLevel + 1);
				stream << "description: " << field.description << "\n";
			}
		}

		void AppendObject(std::ostringstream& stream, const SchemaObject& object, int indentLevel)
		{
			AppendIndent(stream, indentLevel);
			stream << "- object " << object.name << "\n";

			if (!object.description.empty())
			{
				AppendIndent(stream, indentLevel + 1);
				stream << "description: " << object.description << "\n";
			}

			AppendIndent(stream, indentLevel + 1);
			stream << "fields:\n";

			for (const auto& field : object.fields)
			{
				AppendField(stream, field, indentLevel + 2);
			}

			stream << "\n";
		}
	} // anonymous namespace

	std::string DumpSchemaDocument(const SchemaDocument& document)
	{
		std::ostringstream stream;
		stream << "document:\n";

		AppendIndent(stream, 1);
		stream << "source: " << document.srcPath.string() << "\n";

		AppendIndent(stream, 1);
		stream << "object_count: " << document.objects.size() << "\n";

		AppendIndent(stream, 1);
		stream << "objects:\n";

		for (const auto& object : document.objects)
		{
			AppendObject(stream, object, 2);
		}

		return stream.str();
	}
}
