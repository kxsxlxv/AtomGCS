#pragma once

#include "state/SharedState.h"

#include <filesystem>
#include <string>

namespace gcs::config
{

struct AppConfigModel
{
    SharedState::ConnectionSettings connectionSettings;
    SharedState::UiPreferences uiPreferences;
};

[[nodiscard]] AppConfigModel defaultAppConfig();

bool loadAppConfig(const std::filesystem::path &filePath, AppConfigModel &config, std::string &errorMessage);
bool saveAppConfig(const std::filesystem::path &filePath, const AppConfigModel &config, std::string &errorMessage);

bool loadMissionParameters(const std::filesystem::path &filePath,
                           SharedState::MissionParametersModel &missionParameters,
                           std::string &errorMessage);
bool saveMissionParameters(const std::filesystem::path &filePath,
                           const SharedState::MissionParametersModel &missionParameters,
                           std::string &errorMessage);

std::string toConfigString(protocol::FlightMode flightMode);
std::string toConfigString(SharedState::ColorMode colorMode);
protocol::FlightMode flightModeFromConfigString(const std::string &value);
SharedState::ColorMode colorModeFromConfigString(const std::string &value);

}
