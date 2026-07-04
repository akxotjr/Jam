#include "SchemaDumpCommand.h"
#include "CommandUtils.h"

#include <Core/Diagnostic.h>
#include <Inspect/SchemaAstDumper.h>
#include <Parser/JsonSchemaParser.h>

#include <filesystem>
#include <iostream>
#include <string>

using namespace jam::tool;

namespace
{
	struct SchemaDumpOptions
	{
		std::filesystem::path schemaPath;
		std::filesystem::path projectRoot;
	};

	void PrintSchemaDumpUsage()
	{
		std::cout
			<< "Usage:\n"
			<< "  JamTools schema-dump --schema <path> [--project-root <path>]\n";
	}

	bool ParseOptions(int argc, char** argv, SchemaDumpOptions& options)
	{
		for (int i = 0; i < argc; ++i)
		{
			const std::string arg = argv[i];

			auto requireValue = [&](const char* optionName) -> const char*
				{
					if (i + 1 >= argc)
					{
						std::cerr << "Missing value for " << optionName << "\n";
						return nullptr;
					}

					return argv[++i];
				};

			if (arg == "--schema")
			{
				const char* value = requireValue("--schema");
				if (!value) return false;
				options.schemaPath = value;
			}
			else if (arg == "--project-root")
			{
				const char* value = requireValue("--project-root");
				if (!value) return false;
				options.projectRoot = value;
			}
			else if (arg == "--help" || arg == "-h")
			{
				PrintSchemaDumpUsage();
				return false;
			}
			else
			{
				std::cerr << "Unknown option: " << arg << "\n";
				return false;
			}
		}

		if (options.schemaPath.empty())
		{
			std::cerr << "--schema is required.\n";
			return false;
		}

		return true;
	}

}

int RunSchemaDumpCommand(int argc, char** argv)
{
	SchemaDumpOptions options;

	if (!ParseOptions(argc, argv, options))
	{
		PrintSchemaDumpUsage();
		return 1;
	}

	DiagnosticBag diagnostics;
	JsonSchemaParser parser;

	try
	{
		const std::filesystem::path projectRoot = ResolveProjectRoot(options.projectRoot);
		const SchemaDocument document = parser.ParseFile(ResolveProjectPath(projectRoot, options.schemaPath), diagnostics);

		PrintDiagnosticsWithLocation(diagnostics);

		if (diagnostics.HasError())
			return 1;

		std::cout << "# parser-only view\n";
		std::cout << DumpSchemaDocument(document);
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "Schema dump failed: " << e.what() << "\n";
		return 1;
	}
}
