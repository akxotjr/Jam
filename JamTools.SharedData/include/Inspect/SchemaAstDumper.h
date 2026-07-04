#pragma once

#include <string>

#include "AST/SchemaDocument.h"

namespace jam::tool
{
	std::string DumpSchemaDocument(const SchemaDocument& document);
}
