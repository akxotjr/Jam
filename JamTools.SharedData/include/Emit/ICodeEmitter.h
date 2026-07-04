#pragma once

#include "Core/Diagnostic.h"
#include "AST/SchemaDocument.h"
#include "Emit/EmitOptions.h"

namespace jam::tool
{
	class ICodeEmitter
	{
	public:
		virtual ~ICodeEmitter() = default;

		virtual void Emit(const SchemaDocument& document, const EmitOptions& options, DiagnosticBag& diagnostics) = 0;
	};
}
