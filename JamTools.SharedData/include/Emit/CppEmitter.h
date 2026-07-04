#pragma once
#include <string>
#include "ICodeEmitter.h"

namespace jam::tool
{
	class CppEmitter final : public ICodeEmitter
	{
	public:
		void			Emit(const SchemaDocument& document, const EmitOptions& options, DiagnosticBag& diagnostics) override;

	private:
		std::string		EmitHeader(const SchemaDocument& document, const EmitOptions& options) const;
		std::string		EmitPolymorphicBases(const SchemaDocument& document) const;
		std::string		EmitPolymorphicBase(const SchemaPolymorphicType& polymorphicType) const;
		std::string		EmitPolymorphicHelpers(const SchemaDocument& document) const;
		std::string		EmitPolymorphicHelper(const SchemaPolymorphicType& polymorphicType) const;
		std::string		EmitEnums(const SchemaObject& object) const;
		std::string		EmitEnumSerializers(const SchemaObject& object) const;
		std::string		EmitEnumDeclaration(const SchemaObject& object, const SchemaField& field) const;
		std::string		EmitEnumFromJson(const SchemaObject& object, const SchemaField& field, const SchemaEnumType& enumType) const;
		std::string		EmitEnumToJson(const SchemaObject& object, const SchemaField& field, const SchemaEnumType& enumType) const;
		std::string		EmitObject(const SchemaDocument& document, const SchemaObject& object) const;
		std::string		EmitFromJson(const SchemaObject& object) const;
		std::string		EmitToJson(const SchemaObject& object) const;
		std::string		EmitRootJsonHelpers(const SchemaDocument& document) const;
		
		std::string		MakeOutputFileName(const SchemaDocument& document) const;
	};
}
