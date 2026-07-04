#include "ValidateCommand.h"
#include "CommandUtils.h"

#include <Core/Diagnostic.h>
#include <Core/DiagnosticCodes.h>
#include <Parser/JsonSchemaParser.h>
#include <Validation/JsonSchemaValidator.h>
#include <Validation/SchemaSemanticValidator.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace jam::tool;

namespace
{
	enum class eValidateMode
	{
		JsonDocument,
		SchemaDocument
	};

	struct ValidateOptions
	{
		std::filesystem::path inputDir;
		std::filesystem::path inputPath;
		std::filesystem::path validationSchemaPath;
		std::filesystem::path projectRoot;
		bool schemaOnly = false;
		bool semanticOnly = false;
		bool failFast = false;
	};

	void PrintValidateUsage(eValidateMode mode)
	{
		if (mode == eValidateMode::JsonDocument)
		{
			std::cout
				<< "Usage:\n"
				<< "  JamTools validate --json-dir <path> [--schema <path>] [--project-root <path>] [--fail-fast]\n"
				<< "  JamTools validate --json <path> [--schema <path>] [--project-root <path>] [--fail-fast]\n";
			return;
		}

		std::cout
			<< "Usage:\n"
			<< "  JamTools validate-schema --schema-dir <path> [--schema-only|--semantic-only] [--meta-schema <path>] [--project-root <path>] [--fail-fast]\n"
			<< "  JamTools validate-schema --schema <path> [--schema-only|--semantic-only] [--meta-schema <path>] [--project-root <path>] [--fail-fast]\n";
	}

	bool ParseOptions(int argc, char** argv, eValidateMode mode, ValidateOptions& options)
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

			if (mode == eValidateMode::JsonDocument)
			{
				if (arg == "--json-dir")
				{
					const char* value = requireValue("--json-dir");
					if (!value) return false;
					options.inputDir = value;
					continue;
				}

				if (arg == "--json")
				{
					const char* value = requireValue("--json");
					if (!value) return false;
					options.inputPath = value;
					continue;
				}

				if (arg == "--schema")
				{
					const char* value = requireValue("--schema");
					if (!value) return false;
					options.validationSchemaPath = value;
					continue;
				}
			}
			else
			{
				if (arg == "--schema-dir")
				{
					const char* value = requireValue("--schema-dir");
					if (!value) return false;
					options.inputDir = value;
					continue;
				}

				if (arg == "--schema")
				{
					const char* value = requireValue("--schema");
					if (!value) return false;
					options.inputPath = value;
					continue;
				}

				if (arg == "--meta-schema")
				{
					const char* value = requireValue("--meta-schema");
					if (!value) return false;
					options.validationSchemaPath = value;
					continue;
				}

				if (arg == "--schema-only")
				{
					options.schemaOnly = true;
					continue;
				}

				if (arg == "--semantic-only")
				{
					options.semanticOnly = true;
					continue;
				}
			}

			if (arg == "--project-root")
			{
				const char* value = requireValue("--project-root");
				if (!value) return false;
				options.projectRoot = value;
				continue;
			}

			if (arg == "--fail-fast")
			{
				options.failFast = true;
			}
			else if (arg == "--help" || arg == "-h")
			{
				PrintValidateUsage(mode);
				return false;
			}
			else
			{
				std::cerr << "Unknown option: " << arg << "\n";
				return false;
			}
		}

		if (mode == eValidateMode::SchemaDocument
			&& options.schemaOnly
			&& options.semanticOnly)
		{
			std::cerr << "--schema-only and --semantic-only cannot be used together.\n";
			return false;
		}

		if (options.inputDir.empty() == options.inputPath.empty())
		{
			if (mode == eValidateMode::JsonDocument)
				std::cerr << "Specify exactly one of --json-dir or --json.\n";
			else
				std::cerr << "Specify exactly one of --schema-dir or --schema.\n";
			return false;
		}

		return true;
	}

	void AppendDiagnostics(DiagnosticBag& target, const DiagnosticBag& source)
	{
		for (const auto& diagnostic : source.Items())
		{
			target.Add(diagnostic);
		}
	}

	bool LoadJsonFile(
		const std::filesystem::path& path,
		nlohmann::json& root,
		DiagnosticBag& diagnostics)
	{
		std::ifstream file(path);
		if (!file.is_open())
		{
			Diagnostic diagnostic;
			diagnostic.severity = eDiagnosticSeverity::Error;
			diagnostic.code = diag::schema::FileNotFound;
			diagnostic.message = "Could not open schema file.";
			diagnostic.file = path.string();
			diagnostics.Add(std::move(diagnostic));
			return false;
		}

		try
		{
			file >> root;
			return true;
		}
		catch (const std::exception& e)
		{
			Diagnostic diagnostic;
			diagnostic.severity = eDiagnosticSeverity::Error;
			diagnostic.code = diag::schema::JsonParse;
			diagnostic.message = e.what();
			diagnostic.file = path.string();
			diagnostics.Add(std::move(diagnostic));
			return false;
		}
	}

	std::vector<std::filesystem::path> CollectInputFiles(
		const ValidateOptions& options,
		eValidateMode mode,
		const std::filesystem::path& projectRoot)
	{
		if (!options.inputPath.empty())
			return { ResolveProjectPath(projectRoot, options.inputPath) };

		std::vector<std::filesystem::path> files;
		const std::filesystem::path inputDir = ResolveProjectPath(projectRoot, options.inputDir);
		for (const auto& entry : std::filesystem::directory_iterator(inputDir))
		{
			if (!entry.is_regular_file())
				continue;

			if (entry.path().extension() != ".json")
				continue;

			if (mode == eValidateMode::SchemaDocument)
			{
				if (!IsSchemaFilePath(entry.path()))
					continue;
			}
			else
			{
				if (IsSchemaFilePath(entry.path()))
					continue;
			}

			files.push_back(entry.path());
		}

		std::ranges::sort(files);
		return files;
	}

	std::filesystem::path ResolveValidationSchemaPath(
		const std::filesystem::path& inputPath,
		const ValidateOptions& options,
		eValidateMode mode)
	{
		const std::filesystem::path projectRoot = ResolveProjectRoot(options.projectRoot);

		if (!options.validationSchemaPath.empty())
			return ResolveProjectPath(projectRoot, options.validationSchemaPath);

		if (mode == eValidateMode::SchemaDocument)
		{
			if (inputPath.filename().string().ends_with(".schema.json"))
				return ResolveProjectPath(projectRoot, std::filesystem::path("SharedData/Schema/schema-authoring.schema.json"));
			return {};
		}

		const std::filesystem::path siblingSchemaPath =
			inputPath.parent_path() / (inputPath.stem().string() + ".schema.json");
		if (std::filesystem::exists(siblingSchemaPath))
			return siblingSchemaPath;

		const std::filesystem::path sharedSchemaPath =
			ResolveProjectPath(projectRoot, std::filesystem::path("SharedData/Schema") / (inputPath.stem().string() + ".schema.json"));
		if (std::filesystem::exists(sharedSchemaPath))
			return sharedSchemaPath;

		return {};
	}

	int RunValidateInternal(int argc, char** argv, eValidateMode mode)
	{
		ValidateOptions options;

		if (!ParseOptions(argc, argv, mode, options))
		{
			PrintValidateUsage(mode);
			return 1;
		}

		DiagnosticBag diagnostics;
		JsonSchemaValidator schemaValidator;
		JsonSchemaParser parser;
		SchemaSemanticValidator semanticValidator;
		const std::filesystem::path projectRoot = ResolveProjectRoot(options.projectRoot);

		try
		{
			for (const auto& inputPath : CollectInputFiles(options, mode, projectRoot))
			{
				DiagnosticBag fileDiagnostics;
				nlohmann::json root;
				const bool loaded = LoadJsonFile(inputPath, root, fileDiagnostics);
				if (!loaded)
				{
					AppendDiagnostics(diagnostics, fileDiagnostics);
					if (options.failFast)
						break;
					continue;
				}

				const std::filesystem::path validationSchemaPath =
					ResolveValidationSchemaPath(inputPath, options, mode);

				if (mode == eValidateMode::JsonDocument)
				{
					SchemaValidationOptions schemaValidationOptions;
					schemaValidationOptions.sourceName = inputPath.string();
					schemaValidationOptions.schemaPath = validationSchemaPath.string();
					(void)schemaValidator.Validate(root, fileDiagnostics, schemaValidationOptions);
				}
				else
				{
					const bool runSchemaValidation = !options.semanticOnly;
					const bool runSemanticValidation = !options.schemaOnly;

					bool schemaValid = true;
					if (runSchemaValidation)
					{
						SchemaValidationOptions schemaValidationOptions;
						schemaValidationOptions.sourceName = inputPath.string();
						schemaValidationOptions.schemaPath = validationSchemaPath.string();
						schemaValid = schemaValidator.Validate(root, fileDiagnostics, schemaValidationOptions);
					}

					if (runSemanticValidation && schemaValid)
					{
						SchemaDocument document = parser.ParseFile(inputPath, fileDiagnostics);
						if (!fileDiagnostics.HasError())
						{
							semanticValidator.Validate(document, fileDiagnostics);
						}
					}
				}

				AppendDiagnostics(diagnostics, fileDiagnostics);
				if (fileDiagnostics.HasError() && options.failFast)
					break;
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << "Validation failed: " << e.what() << "\n";
			return 1;
		}

		PrintDiagnosticsWithLocation(diagnostics);

		if (diagnostics.HasError())
			return 1;

		std::cout << "Validation completed.\n";
		return 0;
	}
}

int RunValidateCommand(int argc, char** argv)
{
	return RunValidateInternal(argc, argv, eValidateMode::JsonDocument);
}

int RunValidateSchemaCommand(int argc, char** argv)
{
	return RunValidateInternal(argc, argv, eValidateMode::SchemaDocument);
}
