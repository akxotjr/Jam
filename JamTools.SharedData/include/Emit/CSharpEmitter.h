#pragma once

#include "Emit/ICodeEmitter.h"

namespace jam::tool
{
	class CSharpEmitter : public ICodeEmitter
	{
	public:
		void			Emit(const SchemaDocument& document, const EmitOptions& options, DiagnosticBag& diagnostics) override;
	
	private:
		std::string		EmitFile(const SchemaDocument& document, const EmitOptions& options) const;
		std::string		EmitPolymorphicBases(const SchemaDocument& document) const;
		std::string		EmitPolymorphicBase(const SchemaPolymorphicType& polymorphicType) const;
		std::string		EmitPolymorphicConverters(const SchemaDocument& document) const;
		std::string		EmitPolymorphicConverter(const SchemaPolymorphicType& polymorphicType) const;
		std::string		EmitEnums(const SchemaObject& object) const;
		std::string		EmitEnum(const SchemaObject& object, const SchemaField& field) const;
		std::string		EmitObject(const SchemaDocument& document, const SchemaObject& object) const;
		std::string		EmitPropertyDefaultValue(const SchemaField& field) const;

		std::string		MakeOutputFileName(const SchemaDocument& document) const;
	};
}
