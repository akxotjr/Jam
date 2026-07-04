#include "CommandUtils.h"

#include <Windows.h>

#include <iostream>

namespace
{
	std::filesystem::path GetExecutablePath()
	{
		wchar_t buffer[MAX_PATH] = {};
		const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length == 0 || length == MAX_PATH)
			return {};

		return std::filesystem::path(buffer);
	}

	bool IsProjectRootCandidate(const std::filesystem::path& path)
	{
		return std::filesystem::exists(path / "SharedData" / "Schema")
			&& std::filesystem::exists(path / "JamTools")
			&& std::filesystem::exists(path / "JamTools.SharedData");
	}

	std::filesystem::path SearchProjectRoot(std::filesystem::path start)
	{
		if (start.empty())
			return {};

		if (std::filesystem::is_regular_file(start))
			start = start.parent_path();

		start = std::filesystem::weakly_canonical(start);

		for (auto current = start; !current.empty(); current = current.parent_path())
		{
			if (IsProjectRootCandidate(current))
				return current;

			if (current == current.root_path())
				break;
		}

		return {};
	}
}

std::filesystem::path ResolveProjectRoot(const std::filesystem::path& explicitProjectRoot)
{
	if (!explicitProjectRoot.empty())
		return std::filesystem::weakly_canonical(explicitProjectRoot);

	if (const auto cwdRoot = SearchProjectRoot(std::filesystem::current_path()); !cwdRoot.empty())
		return cwdRoot;

	if (const auto exeRoot = SearchProjectRoot(GetExecutablePath()); !exeRoot.empty())
		return exeRoot;

	return {};
}

std::filesystem::path ResolveProjectPath(const std::filesystem::path& projectRoot, const std::filesystem::path& path)
{
	if (path.empty() || path.is_absolute() || projectRoot.empty())
		return path;

	return projectRoot / path;
}

bool IsSchemaFilePath(const std::filesystem::path& path)
{
	return path.filename().string().ends_with(".schema.json");
}

void PrintDiagnosticsWithLocation(const jam::tool::DiagnosticBag& diagnostics)
{
	for (const auto& diagnostic : diagnostics.Items())
	{
		if (!diagnostic.file.empty())
		{
			std::cerr << diagnostic.file;

			if (diagnostic.line > 0)
			{
				std::cerr << ":" << diagnostic.line;
				if (diagnostic.column > 0)
					std::cerr << ":" << diagnostic.column;
			}

			std::cerr << ": ";
		}

		std::cerr << "[" << diagnostic.code << "] " << diagnostic.message << "\n";
	}
}
