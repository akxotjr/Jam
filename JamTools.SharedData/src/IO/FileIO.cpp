#include "IO/FileIO.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace jam::tool
{
	std::string FileIO::ReadAllText(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);

		if (!file)
		{
			throw std::runtime_error("Failed to open file: " + path.string());
		}

		std::ostringstream oss;
		oss << file.rdbuf();

		return oss.str();
	}

	void FileIO::WriteAllText(const std::filesystem::path& path, const std::string& text)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary);
		if (!file)
		{
			throw std::runtime_error("Failed to open file: " + path.string());
		}
	
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
	}
}
