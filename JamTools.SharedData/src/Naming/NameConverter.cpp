#include "Naming/NameConverter.h"

#include <vector>

namespace jam::tool
{
	namespace 
	{
		bool IsDelimiter(char c)
		{
			return !std::isalnum(static_cast<unsigned char>(c));
		}

		char ToLower(char c)
		{
			return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}

		char ToUpper(char c)
		{
			return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		}

		std::string ToLowerAll(std::string_view text)
		{
			std::string result;
			result.reserve(text.size());

			for (char c : text)
			{
				result += ToLower(c);
			}

			return result;
		}

		std::string EnsureLeadingAlpha(std::string value)
		{
			if (value.empty())
				return "Value";

			if (std::isdigit(static_cast<unsigned char>(value.front())))
				return "Value" + value;

			return value;
		}

		std::string EnsureSuffix(std::string value, std::string_view suffix)
		{
			if (value.ends_with(suffix))
				return value;

			value += suffix;
			return value;
		}

		std::vector<std::string> SplitWords(std::string_view text)
		{
			std::vector<std::string> words;
			std::string current;

			auto flush = [&]()
				{
					if (!current.empty())
					{
						words.push_back(ToLowerAll(current));
						current.clear();
					}
				};

			for (char c : text)
			{
				if (IsDelimiter(c))
				{
					flush();
					continue;
				}

				const bool isUpper = std::isupper(static_cast<unsigned char>(c));
				const bool isLower = std::islower(static_cast<unsigned char>(c));
				const bool isDigit = std::isdigit(static_cast<unsigned char>(c));

				if (isUpper && !current.empty())
				{
					const char prev = current.back();
					const bool prevIsLower = std::islower(static_cast<unsigned char>(prev));
					const bool prevIsDigit = std::isdigit(static_cast<unsigned char>(prev));

					if (prevIsLower || prevIsDigit)
						flush();
				}

				if (isLower || isUpper || isDigit)
				{
					current += c;
				}
			}

			flush();
			return words;
		}

	} // anonymous namespace


	std::string NameConverter::ToPascalCase(std::string_view name)
	{
		std::vector<std::string> words = SplitWords(name);
		std::string result;

		for (auto& word : words)
		{
			if (!word.empty())
			{
				word[0] = ToUpper(word[0]);
				result += word;
			}
		}

		return result;
	}

	std::string NameConverter::ToCamelCase(std::string_view name)
	{
		std::vector<std::string> words = SplitWords(name);
		std::string result;

		for (size_t i = 0; i < words.size(); ++i)
		{
			if (!words[i].empty())
			{
				if (i == 0)
				{
					result += ToLowerAll(words[i]);
				}
				else
				{
					words[i][0] = ToUpper(words[i][0]);
					result += words[i];
				}
			}
		}

		return result;
	}

	std::string NameConverter::ToSnakeCase(std::string_view name)
	{
		std::vector<std::string> words = SplitWords(name);
		std::string result;

		for (size_t i = 0; i < words.size(); ++i)
		{
			if (!words[i].empty())
			{
				if (i > 0)
					result += '_';

				result += ToLowerAll(words[i]);
			}
		}

		return result;
	}

	std::string NameConverter::ToCppFieldName(std::string_view jsonName)
	{
		return ToCamelCase(jsonName);
	}

	std::string NameConverter::ToCppTypeName(std::string_view schemaName)
	{
		return ToPascalCase(schemaName);
	}

	std::string NameConverter::ToGeneratedDtoTypeName(std::string_view schemaName)
	{
		return EnsureSuffix(ToCppTypeName(schemaName), "Dto");
	}

	std::string NameConverter::ToGeneratedRootDtoTypeName(std::string_view schemaName)
	{
		std::string typeName = ToCppTypeName(schemaName);

		if (typeName.ends_with("RootDto"))
			return typeName;

		if (typeName.ends_with("Dto"))
			typeName.erase(typeName.size() - 3);

		if (typeName.ends_with("Root"))
			return typeName + "Dto";

		return typeName + "RootDto";
	}

	std::string NameConverter::ToCppEnumMemberName(std::string_view enumValue)
	{
		return EnsureLeadingAlpha(ToPascalCase(enumValue));
	}

	std::string NameConverter::ToCSharpPropertyName(std::string_view jsonName)
	{
		return ToCamelCase(jsonName);
	}

	std::string NameConverter::ToCSharpTypeName(std::string_view schemaName)
	{
		return ToPascalCase(schemaName);
	}

	std::string NameConverter::ToCSharpEnumMemberName(std::string_view enumValue)
	{
		return EnsureLeadingAlpha(ToPascalCase(enumValue));
	}
}
