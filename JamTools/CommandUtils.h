#pragma once

#include <filesystem>
#include <string>

#include <Core/Diagnostic.h>

std::filesystem::path ResolveProjectRoot(const std::filesystem::path& explicitProjectRoot = {});
std::filesystem::path ResolveProjectPath(const std::filesystem::path& projectRoot, const std::filesystem::path& path);
bool IsSchemaFilePath(const std::filesystem::path& path);
void PrintDiagnosticsWithLocation(const jam::tool::DiagnosticBag& diagnostics);
