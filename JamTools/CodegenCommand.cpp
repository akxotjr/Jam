#include "CodegenCommand.h"
#include "CommandUtils.h"

#include <Core/Diagnostic.h>
#include <Emit/CppEmitter.h>
#include <Emit/CSharpEmitter.h>
#include <Emit/EmitOptions.h>
#include <Parser/JsonSchemaParser.h>
#include <Validation/SchemaSemanticValidator.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace jam::tool;

namespace 
{
	struct CodegenOptions
	{
		std::filesystem::path schemaDir;
		std::filesystem::path schemaPath;
		std::filesystem::path outCpp;
		std::filesystem::path outCSharp;
		std::filesystem::path projectRoot;
	};

	void PrintCodegenUsage()
	{
		std::cout
			<< "Usage:\n"
			<< "  JamTools codegen --schema-dir <path> --out-cpp <path> --out-csharp <path> [--project-root <path>]\n"
			<< "  JamTools codegen --schema <path> --out-cpp <path> --out-csharp <path> [--project-root <path>]\n"
			<< "  JamTools check-codegen --schema-dir <path> --out-cpp <path> --out-csharp <path> [--project-root <path>]\n"
			<< "  JamTools check-codegen --schema <path> --out-cpp <path> --out-csharp <path> [--project-root <path>]\n";
	}

	bool ParseOptions(int argc, char** argv, CodegenOptions& options)
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

			if (arg == "--schema-dir")
			{
				const char* value = requireValue("--schema-dir");
				if (!value) return false;
				options.schemaDir = value;
			}
			else if (arg == "--schema")
			{
				const char* value = requireValue("--schema");
				if (!value) return false;
				options.schemaPath = value;
			}
			else if (arg == "--out-cpp")
			{
				const char* value = requireValue("--out-cpp");
				if (!value) return false;
				options.outCpp = value;
			}
			else if (arg == "--out-csharp")
			{
				const char* value = requireValue("--out-csharp");
				if (!value) return false;
				options.outCSharp = value;
			}
			else if (arg == "--project-root")
			{
				const char* value = requireValue("--project-root");
				if (!value) return false;
				options.projectRoot = value;
			}
			else if (arg == "--help" || arg == "-h")
			{
				PrintCodegenUsage();
				return false;
			}
			else
			{
				std::cerr << "Unknown option: " << arg << "\n";
				return false;
			}
		}

		if (options.schemaDir.empty() == options.schemaPath.empty())
		{
			std::cerr << "Specify exactly one of --schema-dir or --schema.\n";
			return false;
		}

		if (options.outCpp.empty() && options.outCSharp.empty())
		{
			std::cerr << "At least one output directory is required.\n";
			return false;
		}

		return true;
	}

	std::vector<std::filesystem::path> CollectSchemaFiles(
		const CodegenOptions& options,
		const std::filesystem::path& projectRoot)
	{
		if (!options.schemaPath.empty())
			return { ResolveProjectPath(projectRoot, options.schemaPath) };

		std::vector<std::filesystem::path> files;
		const std::filesystem::path schemaDir = ResolveProjectPath(projectRoot, options.schemaDir);

		for (const auto& entry : std::filesystem::directory_iterator(schemaDir))
		{
			if (!entry.is_regular_file())
				continue;

			if (!IsSchemaFilePath(entry.path()))
				continue;

			files.push_back(entry.path());
		}

		std::ranges::sort(files);
		return files;
	}

	bool GenerateCode(const CodegenOptions& options, DiagnosticBag& diagnostics)
	{
		const std::filesystem::path projectRoot = ResolveProjectRoot(options.projectRoot);
		JsonSchemaParser		parser;
		SchemaSemanticValidator semanticValidator;
		CppEmitter				cppEmitter;
		CSharpEmitter			csharpEmitter;

		for (const auto& schemaPath : CollectSchemaFiles(options, projectRoot))
		{
			DiagnosticBag fileDiagnostics;
			SchemaDocument document = parser.ParseFile(schemaPath, fileDiagnostics);

			if (fileDiagnostics.HasError())
			{
				for (const auto& diagnostic : fileDiagnostics.Items())
					diagnostics.Add(diagnostic);
				continue;
			}

			semanticValidator.Validate(document, fileDiagnostics);
			if (fileDiagnostics.HasError())
			{
				for (const auto& diagnostic : fileDiagnostics.Items())
					diagnostics.Add(diagnostic);
				continue;
			}

			if (!options.outCpp.empty())
			{
				EmitOptions emitOptions;
				emitOptions.outputDirectory = ResolveProjectPath(projectRoot, options.outCpp);
				cppEmitter.Emit(document, emitOptions, fileDiagnostics);
			}

			if (!options.outCSharp.empty())
			{
				EmitOptions emitOptions;
				emitOptions.outputDirectory = ResolveProjectPath(projectRoot, options.outCSharp);
				csharpEmitter.Emit(document, emitOptions, fileDiagnostics);
			}

			for (const auto& diagnostic : fileDiagnostics.Items())
				diagnostics.Add(diagnostic);
		}

		return !diagnostics.HasError();
	}

	std::vector<std::filesystem::path> CollectRelativeFiles(const std::filesystem::path& root)
	{
		std::vector<std::filesystem::path> files;

		if (!std::filesystem::exists(root))
			return files;

		for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
		{
			if (!entry.is_regular_file())
				continue;

			files.push_back(std::filesystem::relative(entry.path(), root));
		}

		std::ranges::sort(files);
		return files;
	}

	bool CompareFileContents(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
	{
		if (std::filesystem::file_size(lhs) != std::filesystem::file_size(rhs))
			return false;

		std::ifstream lhsStream(lhs, std::ios::binary);
		std::ifstream rhsStream(rhs, std::ios::binary);
		if (!lhsStream.is_open() || !rhsStream.is_open())
			return false;

		std::istreambuf_iterator<char> lhsIt(lhsStream);
		std::istreambuf_iterator<char> rhsIt(rhsStream);
		std::istreambuf_iterator<char> end;

		return std::equal(lhsIt, end, rhsIt);
	}

	bool CompareDirectories(
		const std::filesystem::path& expectedDir,
		const std::filesystem::path& generatedDir,
		const char* label)
	{
		const auto expectedFiles = CollectRelativeFiles(expectedDir);
		const auto generatedFiles = CollectRelativeFiles(generatedDir);

		if (expectedFiles != generatedFiles)
		{
			std::cerr << label << " file set differs from generated output.\n";
			return false;
		}

		for (const auto& relativePath : expectedFiles)
		{
			const auto expectedPath = expectedDir / relativePath;
			const auto generatedPath = generatedDir / relativePath;

			if (!CompareFileContents(expectedPath, generatedPath))
			{
				std::cerr << label << " file differs: " << relativePath.string() << "\n";
				return false;
			}
		}

		return true;
	}
}


int RunCodegenCommand(int argc, char** argv)
{
	CodegenOptions options;

	if (!ParseOptions(argc, argv, options))
	{
		PrintCodegenUsage();
		return 1;
	}

	DiagnosticBag diagnostics;

	try
	{
		GenerateCode(options, diagnostics);
	}
	catch (const std::exception& e)
	{
		std::cerr << "Codegen failed: " << e.what() << "\n";
		return 1;
	}

	PrintDiagnosticsWithLocation(diagnostics);

	if (diagnostics.HasError())
		return 1;

	std::cout << "Codegen completed.\n";
	return 0;
}

int RunCheckCodegenCommand(int argc, char** argv)
{
	CodegenOptions options;

	if (!ParseOptions(argc, argv, options))
	{
		PrintCodegenUsage();
		return 1;
	}

	const auto tempRoot = std::filesystem::temp_directory_path() / std::filesystem::path("jamtools-check-codegen-" + std::to_string(std::rand()));
	const auto tempCpp = tempRoot / "cpp";
	const auto tempCSharp = tempRoot / "csharp";
	const auto cleanupTemp = [&]()
		{
			std::error_code errorCode;
			std::filesystem::remove_all(tempRoot, errorCode);
		};

	const std::filesystem::path projectRoot = ResolveProjectRoot(options.projectRoot);

	if (!options.outCpp.empty())
		std::filesystem::create_directories(tempCpp);

	if (!options.outCSharp.empty())
		std::filesystem::create_directories(tempCSharp);

	CodegenOptions tempOptions = options;
	tempOptions.outCpp = options.outCpp.empty() ? std::filesystem::path{} : tempCpp;
	tempOptions.outCSharp = options.outCSharp.empty() ? std::filesystem::path{} : tempCSharp;

	DiagnosticBag diagnostics;
	bool isUpToDate = true;

	try
	{
		GenerateCode(tempOptions, diagnostics);

		PrintDiagnosticsWithLocation(diagnostics);
		if (diagnostics.HasError())
		{
			cleanupTemp();
			return 1;
		}

		if (!options.outCpp.empty())
			isUpToDate &= CompareDirectories(ResolveProjectPath(projectRoot, options.outCpp), tempCpp, "C++");

		if (!options.outCSharp.empty())
			isUpToDate &= CompareDirectories(ResolveProjectPath(projectRoot, options.outCSharp), tempCSharp, "C#");
	}
	catch (const std::exception& e)
	{
		std::cerr << "check-codegen failed: " << e.what() << "\n";
		cleanupTemp();
		return 1;
	}

	cleanupTemp();

	if (!isUpToDate)
	{
		std::cerr << "Generated code is out of date.\n";
		return 1;
	}

	std::cout << "Generated code is up to date.\n";
	return 0;
}
