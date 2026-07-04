#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace jam::tool
{
	class FileIO
	{
	public:
		static std::string	ReadAllText(const std::filesystem::path& path);
		static void			WriteAllText(const std::filesystem::path& path, const std::string& text);
	};
}
