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
    config.uiPreferences.distancePointSizing = false;
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
        loaded.uiPreferences.distancePointSizing = ui.value("distance_point_sizing", loaded.uiPreferences.distancePointSizing);

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
          {"auto_scroll_log", config.uiPreferences.autoScrollLog},
          {"distance_point_sizing", config.uiPreferences.distancePointSizing}}},
    };

    return saveJsonFile(filePath, document, errorMessage);
}

bool loadMissionParameters(const std::filesystem::path &filePath,
                           SharedState::MissionParametersModel &missionParameters,
                           std::string &errorMessage)
{
    missionParameters.payload.delayedStartTimeSec = 0;
    missionParameters.payload.takeoffAltitudeM = 10.0f;
    missionParameters.payload.flightSpeedMS = 5.0f;
    missionParameters.payload.numPoints = 0;
    missionParameters.flightMode = protocol::FlightMode::AUTOMATIC;
    missionParameters.pointsNed.clear();

    if (!std::filesystem::exists(filePath))
    {
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

        if (document.contains("points_ned"))
        {
            const auto &points = document.at("points_ned");
            if (!points.is_array())
            {
                errorMessage = "points_ned must be an array";
                return false;
            }

            missionParameters.pointsNed.reserve(points.size());
            for (const auto &pointJson : points)
            {
                protocol::MissionPointNed point{};
                point.northM = pointJson.at("north_m").get<float>();
                point.eastM = pointJson.at("east_m").get<float>();
                point.downM = pointJson.at("down_m").get<float>();
                missionParameters.pointsNed.push_back(point);
            }
        }

        missionParameters.payload.numPoints = static_cast<std::uint32_t>(missionParameters.pointsNed.size());
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
    json points = json::array();
    for (const auto &point : missionParameters.pointsNed)
    {
        points.push_back({
            {"north_m", point.northM},
            {"east_m", point.eastM},
            {"down_m", point.downM},
        });
    }

    const json document = {
        {"delayed_start_time_sec", missionParameters.payload.delayedStartTimeSec},
        {"takeoff_altitude_m", missionParameters.payload.takeoffAltitudeM},
        {"flight_speed_m_s", missionParameters.payload.flightSpeedMS},
        {"flight_mode", toConfigString(missionParameters.flightMode)},
        {"points_ned", points},
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

} // namespace gcs::config