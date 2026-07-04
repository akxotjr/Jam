#include <string>
#include <iostream>

#include "CodegenCommand.h"
#include "SchemaDumpCommand.h"
#include "ValidateCommand.h"

namespace 
{
	void PrintUsage()
	{
		std::cout
			<< "JamTools - A tool for working with Jamfiles\n"
			<< "\n"
			<< "Usage:\n"
			<< "  JamTools <command> [options]\n"
			<< "\n"
			<< "Commands:\n"
			<< "  codegen  - Generate code from Jamfiles\n"
			<< "  check-codegen - Verify generated code is up to date\n"
			<< "  schema-dump - Dump parsed schema AST\n"
			<< "  validate - Validate JSON documents against *.schema.json\n"
			<< "  validate-schema - Validate schema authoring documents\n";
	}
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		PrintUsage();
		return 1;
	}

	std::string command = argv[1];

	if (command == "codegen")
	{
		return RunCodegenCommand(argc - 2, argv + 2);
	}

	if (command == "check-codegen")
	{
		return RunCheckCodegenCommand(argc - 2, argv + 2);
	}

	if (command == "schema-dump")
	{
		return RunSchemaDumpCommand(argc - 2, argv + 2);
	}

	if (command == "validate")
	{
		return RunValidateCommand(argc - 2, argv + 2);
	}

	if (command == "validate-schema")
	{
		return RunValidateSchemaCommand(argc - 2, argv + 2);
	}

	if (command == "docs")
	{
		// todo: implement docs command
		return 0;
	}

	if (command == "hash")
	{
		// todo: implement hash command
		return 0;
	}

	if (command == "version")
	{
		// todo: implement version command
		return 0;
	}

	if (command == "help")
	{
		PrintUsage();
		return 0;
	}

	std::cerr << "Unknown command: " << command << "\n";
	PrintUsage();
	return 1;
}
