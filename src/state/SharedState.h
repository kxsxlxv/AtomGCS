#pragma once

#include "shared/protocol/protocol.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <shared_mutex>
#include <string>
#include <vector>

namespace gcs
{

    class SharedState
    {
    public:
        enum class ConnectionStatus : std::uint8_t
        {
            disconnected,
            connecting,
            connected,
        };

        enum class LogDirection : std::uint8_t
        {
            outbound,
            inbound,
            local,
        };

        enum class LogCategory : std::uint8_t
        {
            command,
            telemetry,
            error,
        };

        enum class ColorMode : std::uint8_t
        {
            intensity,
            distance,
        };

        struct ConnectionSettings
        {
            std::string ipAddress = "127.0.0.1";
            std::uint16_t tcpPort = 5760;
            std::uint16_t udpPort = 5761;
        };

        struct MissionParametersModel
        {
            protocol::PayloadMissionParamsHeader payload{};
            protocol::FlightMode flightMode = protocol::FlightMode::AUTOMATIC;
            std::vector<protocol::MissionPointNed> pointsNed;
        };

        struct SimulationModel
        {
            protocol::PayloadSimObstacles obstacles{};
            protocol::PayloadSimLidar lidar{};
        };

        struct UiPreferences
        {
            float pointSize = 2.0f;
            ColorMode colorMode = ColorMode::intensity;
            std::size_t logBufferSize = 1000;
            bool autoScrollLog = true;
            bool distancePointSizing = false;
        };

        struct LogEntry
        {
            std::chrono::system_clock::time_point timestamp;
            LogDirection direction = LogDirection::local;
            LogCategory category = LogCategory::telemetry;
            std::string type;
            std::string description;
        };

        struct PointCloudFrame
        {
            std::uint32_t timestampMs = 0;
            std::uint64_t revision = 0;
            std::vector<protocol::PointCloudPoint> points;
        };

        struct CommandFeedback
        {
            bool active = false;
            bool pending = false;
            protocol::AckResult result = protocol::AckResult::INTERNAL_ERROR;
            std::chrono::steady_clock::time_point sentAt{};
            std::chrono::steady_clock::time_point updatedAt{};
            std::string message;
        };

        struct Snapshot
        {
            ConnectionStatus connectionStatus = ConnectionStatus::disconnected;
            ConnectionSettings connectionSettings;
            MissionParametersModel missionParameters;
            SimulationModel simulation;
            UiPreferences uiPreferences;
            protocol::PayloadTelemetryState telemetryState{};
            protocol::PayloadTelemetryPosition telemetryPosition{};
            PointCloudFrame pointCloud;
            std::vector<LogEntry> logs;
            std::array<CommandFeedback, 32> commandFeedbacks{};
        };

        SharedState();

        [[nodiscard]] Snapshot snapshot() const;

        void setConnectionStatus(ConnectionStatus status);
        [[nodiscard]] ConnectionStatus getConnectionStatus() const;

        void setConnectionSettings(ConnectionSettings settings);
        [[nodiscard]] ConnectionSettings getConnectionSettings() const;

        void setMissionParameters(MissionParametersModel missionParameters);
        [[nodiscard]] MissionParametersModel getMissionParameters() const;

        void setSimulationModel(SimulationModel simulation);
        [[nodiscard]] SimulationModel getSimulationModel() const;

        void setUiPreferences(UiPreferences preferences);
        [[nodiscard]] UiPreferences getUiPreferences() const;

        void updateTelemetryState(const protocol::PayloadTelemetryState &telemetryState);
        [[nodiscard]] protocol::PayloadTelemetryState getTelemetryState() const;

        void updateTelemetryPosition(const protocol::PayloadTelemetryPosition &telemetryPosition);
        [[nodiscard]] protocol::PayloadTelemetryPosition getTelemetryPosition() const;

        void updatePointCloud(std::uint32_t timestampMs, std::vector<protocol::PointCloudPoint> points);
        [[nodiscard]] PointCloudFrame getPointCloud() const;

        void appendLog(LogDirection direction, LogCategory category, std::string type, std::string description);
        void clearLogs();
        [[nodiscard]] std::vector<LogEntry> getLogs() const;

        void markCommandSent(protocol::CommandId commandId);
        void applyAck(const protocol::PayloadAck &ack);
        [[nodiscard]] std::array<CommandFeedback, 32> getCommandFeedbacks() const;

    private:
        static constexpr std::size_t commandFeedbackCapacity = 32;

        void trimLogsLocked();
        static std::size_t commandIndex(protocol::CommandId commandId);

        mutable std::shared_mutex mutex;
        ConnectionStatus connectionStatus = ConnectionStatus::disconnected;
        ConnectionSettings connectionSettings;
        MissionParametersModel missionParameters;
        SimulationModel simulation;
        UiPreferences uiPreferences;
        protocol::PayloadTelemetryState telemetryState{};
        protocol::PayloadTelemetryPosition telemetryPosition{};
        PointCloudFrame pointCloud;
        std::deque<LogEntry> logs;
        std::array<CommandFeedback, commandFeedbackCapacity> commandFeedbacks{};
    };

    const char *toString(SharedState::ConnectionStatus status);
    const char *toString(SharedState::ColorMode colorMode);

} // namespace gcs