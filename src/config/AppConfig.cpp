#include "config/AppConfig.h"

#include <fstream>

#include <nlohmann/json.hpp>

namespace gcs::config
{

namespace
{
using nlohmann::json;

AppConfigModel buildDefaultConfig()
{
    AppConfigModel config;
    config.connectionSettings.ipAddress = "127.0.0.1";
    config.connectionSettings.tcpPort = 5760;
    config.connectionSettings.udpPort = 5761;
    config.uiPreferences.pointSize = 2.0f;
    config.uiPreferences.colorMode = SharedState::ColorMode::intensity;
    config.uiPreferences.logBufferSize = 1000;
    config.uiPreferences.autoScrollLog = true;
    return config;
}

bool loadJsonFile(const std::filesystem::path &filePath, json &document, std::string &errorMessage)
{
    std::ifstream input(filePath);
    if (!input.is_open())
    {
        errorMessage = "Failed to open file";
        return false;
    }

    try
    {
        input >> document;
        return true;
    }
    catch (const std::exception &exception)
    {
        errorMessage = exception.what();
        return false;
    }
}

bool saveJsonFile(const std::filesystem::path &filePath, const json &document, std::string &errorMessage)
{
    std::ofstream output(filePath);
    if (!output.is_open())
    {
        errorMessage = "Failed to open file for writing";
        return false;
    }

    try
    {
        output << document.dump(4);
        return true;
    }
    catch (const std::exception &exception)
    {
        errorMessage = exception.what();
        return false;
    }
}
} // namespace

AppConfigModel defaultAppConfig()
{
    return buildDefaultConfig();
}

bool loadAppConfig(const std::filesystem::path &filePath, AppConfigModel &config, std::string &errorMessage)
{
    if (!std::filesystem::exists(filePath))
    {
        config = buildDefaultConfig();
        return true;
    }

    json document;
    if (!loadJsonFile(filePath, document, errorMessage))
    {
        return false;
    }

    try
    {
        AppConfigModel loaded = buildDefaultConfig();
        const auto &connection = document.at("connection");
        loaded.connectionSettings.ipAddress = connection.value("ip", loaded.connectionSettings.ipAddress);
        loaded.connectionSettings.tcpPort = connection.value("tcp_port", loaded.connectionSettings.tcpPort);
        loaded.connectionSettings.udpPort = connection.value("udp_port", loaded.connectionSettings.udpPort);

        const auto &ui = document.at("ui");
        loaded.uiPreferences.pointSize = ui.value("point_size", loaded.uiPreferences.pointSize);
        loaded.uiPreferences.colorMode = colorModeFromConfigString(ui.value("color_mode", std::string("intensity")));
        loaded.uiPreferences.logBufferSize = ui.value("log_buffer_size", loaded.uiPreferences.logBufferSize);
        loaded.uiPreferences.autoScrollLog = ui.value("auto_scroll_log", loaded.uiPreferences.autoScrollLog);

        config = loaded;
        return true;
    }
    catch (const std::exception &exception)
    {
        errorMessage = exception.what();
        return false;
    }
}

bool saveAppConfig(const std::filesystem::path &filePath, const AppConfigModel &config, std::string &errorMessage)
{
    const json document = {
        {"connection",
         {{"ip", config.connectionSettings.ipAddress},
          {"tcp_port", config.connectionSettings.tcpPort},
          {"udp_port", config.connectionSettings.udpPort}}},
        {"ui",
         {{"point_size", config.uiPreferences.pointSize},
          {"color_mode", toConfigString(config.uiPreferences.colorMode)},
          {"log_buffer_size", config.uiPreferences.logBufferSize},
          {"auto_scroll_log", config.uiPreferences.autoScrollLog}}},
    };

    return saveJsonFile(filePath, document, errorMessage);
}

bool loadMissionParameters(const std::filesystem::path &filePath,
                           SharedState::MissionParametersModel &missionParameters,
                           std::string &errorMessage)
{
    if (!std::filesystem::exists(filePath))
    {
        missionParameters.payload.delayedStartTimeSec = 0;
        missionParameters.payload.takeoffAltitudeM = 10.0f;
        missionParameters.payload.flightSpeedMS = 5.0f;
        missionParameters.flightMode = protocol::FlightMode::AUTOMATIC;
        return true;
    }

    json document;
    if (!loadJsonFile(filePath, document, errorMessage))
    {
        return false;
    }

    try
    {
        missionParameters.payload.delayedStartTimeSec = document.value("delayed_start_time_sec", 0U);
        missionParameters.payload.takeoffAltitudeM = document.value("takeoff_altitude_m", 10.0f);
        missionParameters.payload.flightSpeedMS = document.value("flight_speed_m_s", 5.0f);
        missionParameters.flightMode =
            flightModeFromConfigString(document.value("flight_mode", std::string("automatic")));
        return true;
    }
    catch (const std::exception &exception)
    {
        errorMessage = exception.what();
        return false;
    }
}

bool saveMissionParameters(const std::filesystem::path &filePath,
                           const SharedState::MissionParametersModel &missionParameters,
                           std::string &errorMessage)
{
    const json document = {
        {"delayed_start_time_sec", missionParameters.payload.delayedStartTimeSec},
        {"takeoff_altitude_m", missionParameters.payload.takeoffAltitudeM},
        {"flight_speed_m_s", missionParameters.payload.flightSpeedMS},
        {"flight_mode", toConfigString(missionParameters.flightMode)},
    };

    return saveJsonFile(filePath, document, errorMessage);
}

std::string toConfigString(protocol::FlightMode flightMode)
{
    switch (flightMode)
    {
    case protocol::FlightMode::AUTOMATIC:
        return "automatic";
    case protocol::FlightMode::SEMI_AUTOMATIC:
        return "semi_automatic";
    }

    return "automatic";
}

std::string toConfigString(SharedState::ColorMode colorMode)
{
    switch (colorMode)
    {
    case SharedState::ColorMode::intensity:
        return "intensity";
    case SharedState::ColorMode::distance:
        return "distance";
    }

    return "intensity";
}

protocol::FlightMode flightModeFromConfigString(const std::string &value)
{
    if (value == "semi_automatic")
    {
        return protocol::FlightMode::SEMI_AUTOMATIC;
    }

    return protocol::FlightMode::AUTOMATIC;
}

SharedState::ColorMode colorModeFromConfigString(const std::string &value)
{
    if (value == "distance")
    {
        return SharedState::ColorMode::distance;
    }

    return SharedState::ColorMode::intensity;
}

}
