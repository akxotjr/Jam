#pragma once

#include <filesystem>
#include <string>

namespace jam::tool
{
	struct EmitOptions
	{
		std::filesystem::path	outputDirectory;
		std::string				cppNamespace	= "jam::shared::gen";
		std::string				csharpNamespace	= "JamUnity.SharedData.Generated";
	
		bool					emitSerializer	= true;
		bool					overWrite		= true;
	};
}
