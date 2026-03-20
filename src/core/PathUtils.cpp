#include "core/PathUtils.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace gcs
{

    namespace
    {

        std::filesystem::path getExecutablePath()
        {
        #ifdef _WIN32
            std::wstring buffer(MAX_PATH, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                throw std::runtime_error("GetModuleFileNameW failed");
            }
            buffer.resize(length);
            return std::filesystem::path(buffer);
        #else
            std::string buffer(PATH_MAX, '\0');
            const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
            if (length <= 0)
            {
                throw std::runtime_error("readlink(/proc/self/exe) failed");
            }
            buffer.resize(static_cast<std::size_t>(length));
            return std::filesystem::path(buffer);
        #endif
    }

    }

    ApplicationPaths buildApplicationPaths()
    {
        ApplicationPaths paths;
        paths.executableFile = getExecutablePath();
        paths.executableDir = paths.executableFile.parent_path();
        paths.resourcesDir = paths.executableDir / "resources";
        paths.appConfigFile = paths.executableDir / "app_config.json";
        paths.missionParametersFile = paths.executableDir / "mission_params.json";
        return paths;
    }

    std::string readTextFile(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("Unable to open file: " + path.string());
        }

        std::ostringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }
}