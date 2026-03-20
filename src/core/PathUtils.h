#pragma once

#include <filesystem>
#include <string>

namespace gcs
{

    struct ApplicationPaths
    {
        std::filesystem::path executableFile;
        std::filesystem::path executableDir;
        std::filesystem::path resourcesDir;
        std::filesystem::path appConfigFile;
        std::filesystem::path missionParametersFile;
    };

    ApplicationPaths buildApplicationPaths();
    std::string readTextFile(const std::filesystem::path &path);

}