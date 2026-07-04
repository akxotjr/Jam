#pragma once

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace jam
{
	struct JsonFileIO
	{
		using json = nlohmann::json;

		template <typename ValidateFn>
		static json Load(const std::string& path, const std::string& openError, ValidateFn&& validate)
		{
			std::ifstream ifs(path);
			if (!ifs.is_open())
				throw std::runtime_error(openError + path);

			json j;
			ifs >> j;
			std::forward<ValidateFn>(validate)(j);
			return j;
		}

		template <typename ValidateFn>
		static void Save(const std::string& path, const json& j, const std::string& openError, ValidateFn&& validate)
		{
			std::forward<ValidateFn>(validate)(j);

			std::ofstream ofs(path);
			if (!ofs.is_open())
				throw std::runtime_error(openError + path);

			ofs << j.dump(2);
		}
	};
}
