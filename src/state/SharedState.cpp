#include "state/SharedState.h"

#include "shared/protocol/protocol_utils.h"

#include <algorithm>
#include <mutex>
#include <utility>

namespace gcs
{

    namespace
    {
        protocol::PayloadTelemetryState makeDefaultTelemetryState()
        {
            protocol::PayloadTelemetryState state{};
            state.currentState = protocol::DroneState::DISCONNECTED;
            state.availableCommands = 0;
            state.flightMode = protocol::FlightMode::AUTOMATIC;
            state.batteryPercent = 0;
            return state;
        }
    } // namespace

    SharedState::SharedState()
    {
        missionParameters.payload.delayedStartTimeSec = 0;
        missionParameters.payload.takeoffAltitudeM = 10.0f;
        missionParameters.payload.flightSpeedMS = 5.0f;
        missionParameters.payload.numPoints = 0;
        simulation.lidar.lidarActive = true;
        telemetryState = makeDefaultTelemetryState();
    }

    SharedState::Snapshot SharedState::snapshot() const
    {
        std::shared_lock lock(mutex);

        Snapshot copy;
        copy.connectionStatus = connectionStatus;
        copy.connectionSettings = connectionSettings;
        copy.missionParameters = missionParameters;
        copy.simulation = simulation;
        copy.uiPreferences = uiPreferences;
        copy.telemetryState = telemetryState;
        copy.telemetryPosition = telemetryPosition;
        copy.pointCloud = pointCloud;
        copy.logs.assign(logs.begin(), logs.end());
        copy.commandFeedbacks = commandFeedbacks;
        return copy;
    }

    void SharedState::setConnectionStatus(ConnectionStatus status)
    {
        std::unique_lock lock(mutex);
        connectionStatus = status;
        if (status == ConnectionStatus::disconnected)
        {
            telemetryState = makeDefaultTelemetryState();
        }
    }

    SharedState::ConnectionStatus SharedState::getConnectionStatus() const
    {
        std::shared_lock lock(mutex);
        return connectionStatus;
    }

    void SharedState::setConnectionSettings(ConnectionSettings settings)
    {
        std::unique_lock lock(mutex);
        connectionSettings = std::move(settings);
    }

    SharedState::ConnectionSettings SharedState::getConnectionSettings() const
    {
        std::shared_lock lock(mutex);
        return connectionSettings;
    }

    void SharedState::setMissionParameters(MissionParametersModel newMissionParameters)
    {
        std::unique_lock lock(mutex);
        missionParameters = newMissionParameters;
    }

    SharedState::MissionParametersModel SharedState::getMissionParameters() const
    {
        std::shared_lock lock(mutex);
        return missionParameters;
    }

    void SharedState::setSimulationModel(SimulationModel newSimulation)
    {
        std::unique_lock lock(mutex);
        simulation = newSimulation;
    }

    SharedState::SimulationModel SharedState::getSimulationModel() const
    {
        std::shared_lock lock(mutex);
        return simulation;
    }

    void SharedState::setUiPreferences(UiPreferences preferences)
    {
        std::unique_lock lock(mutex);
        uiPreferences = preferences;
        trimLogsLocked();
    }

    SharedState::UiPreferences SharedState::getUiPreferences() const
    {
        std::shared_lock lock(mutex);
        return uiPreferences;
    }

    void SharedState::updateTelemetryState(const protocol::PayloadTelemetryState &newTelemetryState)
    {
        std::unique_lock lock(mutex);
        telemetryState = newTelemetryState;
    }

    protocol::PayloadTelemetryState SharedState::getTelemetryState() const
    {
        std::shared_lock lock(mutex);
        return telemetryState;
    }

    void SharedState::updateTelemetryPosition(const protocol::PayloadTelemetryPosition &newTelemetryPosition)
    {
        std::unique_lock lock(mutex);
        telemetryPosition = newTelemetryPosition;
    }

    protocol::PayloadTelemetryPosition SharedState::getTelemetryPosition() const
    {
        std::shared_lock lock(mutex);
        return telemetryPosition;
    }

    void SharedState::updatePointCloud(std::uint32_t timestampMs, std::vector<protocol::PointCloudPoint> points)
    {
        std::unique_lock lock(mutex);
        pointCloud.timestampMs = timestampMs;
        pointCloud.revision += 1;
        pointCloud.points = std::move(points);
    }

    SharedState::PointCloudFrame SharedState::getPointCloud() const
    {
        std::shared_lock lock(mutex);
        return pointCloud;
    }

    void SharedState::appendLog(LogDirection direction, LogCategory category, std::string type, std::string description)
    {
        std::unique_lock lock(mutex);
        logs.push_back(LogEntry{
            .timestamp = std::chrono::system_clock::now(),
            .direction = direction,
            .category = category,
            .type = std::move(type),
            .description = std::move(description),
        });
        trimLogsLocked();
    }

    void SharedState::clearLogs()
    {
        std::unique_lock lock(mutex);
        logs.clear();
    }

    std::vector<SharedState::LogEntry> SharedState::getLogs() const
    {
        std::shared_lock lock(mutex);
        return {logs.begin(), logs.end()};
    }

    void SharedState::markCommandSent(protocol::CommandId commandId)
    {
        const auto now = std::chrono::steady_clock::now();
        std::unique_lock lock(mutex);
        auto &feedback = commandFeedbacks[commandIndex(commandId)];
        feedback.active = true;
        feedback.pending = true;
        feedback.result = protocol::AckResult::INTERNAL_ERROR;
        feedback.sentAt = now;
        feedback.updatedAt = now;
        feedback.message.clear();
    }

    void SharedState::applyAck(const protocol::PayloadAck &ack)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto ackResult = static_cast<protocol::AckResult>(ack.result);
        std::unique_lock lock(mutex);

        if (ack.originalMsgType == protocol::MsgType::CMD_COMMAND &&
            ack.originalCommandId >= protocol::CommandId::PREPARE &&
            static_cast<std::uint8_t>(ack.originalCommandId) <= commandFeedbackCapacity)
        {
            auto &feedback = commandFeedbacks[commandIndex(ack.originalCommandId)];
            feedback.active = true;
            feedback.pending = false;
            feedback.result = ackResult;
            feedback.updatedAt = now;
            feedback.message = protocol::ackMessageToString(ack);
        }
    }

    std::array<SharedState::CommandFeedback, 32> SharedState::getCommandFeedbacks() const
    {
        std::shared_lock lock(mutex);
        return commandFeedbacks;
    }

    void SharedState::trimLogsLocked()
    {
        const auto bufferSize = std::max<std::size_t>(1, uiPreferences.logBufferSize);
        while (logs.size() > bufferSize)
        {
            logs.pop_front();
        }
    }

    std::size_t SharedState::commandIndex(protocol::CommandId commandId)
    {
        const auto rawValue = static_cast<std::uint8_t>(commandId);
        return rawValue == 0 ? 0 : static_cast<std::size_t>(rawValue - 1U);
    }

    const char *toString(SharedState::ConnectionStatus status)
    {
        switch (status)
        {
        case SharedState::ConnectionStatus::disconnected:
            return "Отключено";
        case SharedState::ConnectionStatus::connecting:
            return "Подключение";
        case SharedState::ConnectionStatus::connected:
            return "Подключено";
        }

        return "Unknown";
    }

    const char *toString(SharedState::ColorMode colorMode)
    {
        switch (colorMode)
        {
        case SharedState::ColorMode::intensity:
            return "Intensity";
        case SharedState::ColorMode::distance:
            return "Distance";
        }

        return "Unknown";
    }


} // namespace gcs

