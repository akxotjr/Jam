#include "PhysicsAssetFlattenCommand.h"
#include "CommandUtils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <array>
#include <nlohmann/json.hpp>

namespace
{
    using json = nlohmann::json;

    struct Options
    {
        std::filesystem::path common;
        std::filesystem::path world;
        std::filesystem::path output;
        std::filesystem::path projectRoot;
    };

    constexpr std::array Sections = {
        "materials", "meshes", "shapes", "dyn_bodies", "cct_bodies",
        "char_move_configs", "kinematic_driver_configs", "projectile_configs", "archetypes"
    };

    void PrintUsage()
    {
        std::cout
            << "Usage:\n"
            << "  JamTools flatten-physics --common <path> [--world <path>] --out <path> [--project-root <path>]\n";
    }

    bool ParseOptions(int argc, char** argv, Options& options)
    {
        for (int i = 0; i < argc; ++i)
        {
            const std::string arg = argv[i];
            auto requireValue = [&](const char* name) -> const char*
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "Missing value for " << name << "\n";
                    return nullptr;
                }
                return argv[++i];
            };

            if (arg == "--common")
            {
                const char* value = requireValue("--common");
                if (!value) return false;
                options.common = value;
            }
            else if (arg == "--world")
            {
                const char* value = requireValue("--world");
                if (!value) return false;
                options.world = value;
            }
            else if (arg == "--out")
            {
                const char* value = requireValue("--out");
                if (!value) return false;
                options.output = value;
            }
            else if (arg == "--project-root")
            {
                const char* value = requireValue("--project-root");
                if (!value) return false;
                options.projectRoot = value;
            }
            else if (arg == "--help" || arg == "-h")
            {
                PrintUsage();
                return false;
            }
            else
            {
                std::cerr << "Unknown option: " << arg << "\n";
                return false;
            }
        }

        if (options.common.empty() || options.output.empty())
        {
            std::cerr << "--common and --out are required.\n";
            return false;
        }
        return true;
    }

    bool Load(const std::filesystem::path& path, json& document)
    {
        std::ifstream stream(path);
        if (!stream.is_open())
        {
            std::cerr << "Could not open physics source: " << path.string() << "\n";
            return false;
        }
        try
        {
            stream >> document;
            return true;
        }
        catch (const std::exception& exception)
        {
            std::cerr << "Could not parse physics source '" << path.string() << "': " << exception.what() << "\n";
            return false;
        }
    }

    bool ValidateComposition(const json& document, std::string_view expectedScope, std::string_view sourceName)
    {
        if (!document.contains("composition") || !document["composition"].is_object())
        {
            std::cerr << sourceName << " physics source has no composition metadata.\n";
            return false;
        }
        const json& composition = document["composition"];
        if (composition.value("scope", std::string{}) != expectedScope)
        {
            std::cerr << sourceName << " physics source must use composition.scope='" << expectedScope << "'.\n";
            return false;
        }
        return true;
    }

    bool MergeSection(json& output, const json& source, const char* section)
    {
        if (!source.contains(section))
            return true;
        if (!source[section].is_object())
        {
            std::cerr << "Physics section '" << section << "' must be an object.\n";
            return false;
        }

        json& target = output[section];
        if (!target.is_object())
            target = json::object();
        for (const auto& [name, value] : source[section].items())
        {
            if (target.contains(name))
            {
                std::cerr << "Duplicate physics definition '" << section << "." << name << "'.\n";
                return false;
            }
            target[name] = value;
        }
        return true;
    }
}

int RunPhysicsAssetFlattenCommand(int argc, char** argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
        return 1;

    const std::filesystem::path projectRoot = ResolveProjectRoot(options.projectRoot);
    const std::filesystem::path commonPath = ResolveProjectPath(projectRoot, options.common);
    const std::filesystem::path outputPath = ResolveProjectPath(projectRoot, options.output);

    json common;
    if (!Load(commonPath, common) || !ValidateComposition(common, "common", "Common"))
        return 1;

    json effective = common;
    effective.erase("composition");
    for (const char* section : Sections)
        if (!effective.contains(section)) effective[section] = json::object();

    if (!options.world.empty())
    {
        const std::filesystem::path worldPath = ResolveProjectPath(projectRoot, options.world);
        json world;
        if (!Load(worldPath, world) || !ValidateComposition(world, "world", "World"))
            return 1;

        const json& includes = world["composition"].value("includes", json::array());
        if (!includes.is_array() || includes.size() != 1 || includes[0] != "Common")
        {
            std::cerr << "M1 world physics source must include exactly 'Common'.\n";
            return 1;
        }
        if (world.value("version", 0) != effective.value("version", 0))
        {
            std::cerr << "Physics source versions do not match.\n";
            return 1;
        }
        for (const char* section : Sections)
            if (!MergeSection(effective, world, section)) return 1;
    }

    std::filesystem::create_directories(outputPath.parent_path());
    std::ofstream stream(outputPath, std::ios::trunc);
    if (!stream.is_open())
    {
        std::cerr << "Could not write effective physics asset: " << outputPath.string() << "\n";
        return 1;
    }
    stream << effective.dump(2) << '\n';
    return 0;
}
