#include "network/ProtocolClient.h"

#include "shared/protocol/protocol_utils.h"

#include <cstring>
#include <sstream>

namespace gcs::network
{

    namespace
    {
        std::string buildPositionSummary(const protocol::PayloadTelemetryPosition &position)
        {
            std::ostringstream stream;
            stream << "Position (" << position.posX << ", " << position.posY << ", " << position.posZ << ")";
            return stream.str();
        }

        std::string buildStateSummary(const protocol::PayloadTelemetryState &state)
        {
            std::ostringstream stream;
            stream << protocol::droneStateToString(static_cast<protocol::DroneState>(state.currentState)) << ", battery "
                << static_cast<int>(state.batteryPercent) << "%";
            return stream.str();
        }

        std::string buildPointCloudSummary(const protocol::PayloadPointCloudHeader &header)
        {
            std::ostringstream stream;
            stream << "Point cloud frame: " << header.numPoints << " points";
            return stream.str();
        }
    }

    ProtocolClient::ProtocolClient(SharedState &sharedStateValue)
        : sharedState(sharedStateValue),
        tcpClient(
            [this](std::vector<std::uint8_t> packetBytes) { handleTcpPacket(std::move(packetBytes)); },
            [this](SharedState::ConnectionStatus status, std::string message) {
                handleTcpStatus(status, std::move(message));
            }),
        udpReceiver(
            [this](std::vector<std::uint8_t> packetBytes) { handleUdpPacket(std::move(packetBytes)); },
            [this](std::string message) { handleUdpError(std::move(message)); })
    {
    }

    void ProtocolClient::connect()
    {
        const auto settings = sharedState.getConnectionSettings();
        sharedState.setConnectionStatus(SharedState::ConnectionStatus::connecting);
        sharedState.appendLog(SharedState::LogDirection::local,
                            SharedState::LogCategory::telemetry,
                            "CONNECTION",
                            "Connecting to " + settings.ipAddress + ":" + std::to_string(settings.tcpPort));

        udpReceiver.start(settings.udpPort);
        tcpClient.start(settings);
    }

    void ProtocolClient::disconnect()
    {
        tcpClient.stop();
        udpReceiver.stop();
        sharedState.setConnectionStatus(SharedState::ConnectionStatus::disconnected);
        sharedState.appendLog(
            SharedState::LogDirection::local, SharedState::LogCategory::telemetry, "CONNECTION", "Disconnected");
    }

    bool ProtocolClient::sendCommand(protocol::CommandId commandId)
    {
        protocol::PayloadCommand payload{static_cast<std::uint8_t>(commandId)};
        sharedState.markCommandSent(commandId);
        return sendPacket(protocol::MsgType::CMD_COMMAND,
                        protocol::serializePacket(protocol::MsgType::CMD_COMMAND, payload),
                        SharedState::LogCategory::command,
                        protocol::commandIdToString(commandId));
    }

    bool ProtocolClient::sendMissionParameters(const SharedState::MissionParametersModel &missionParameters)
    {
        return sendPacket(protocol::MsgType::CMD_SET_PARAMS,
                        protocol::serializePacket(protocol::MsgType::CMD_SET_PARAMS, missionParameters.payload),
                        SharedState::LogCategory::command,
                        "Mission parameters");
    }

    bool ProtocolClient::sendMode(protocol::FlightMode flightMode)
    {
        protocol::PayloadSetMode payload{static_cast<std::uint8_t>(flightMode)};
        return sendPacket(protocol::MsgType::CMD_SET_MODE,
                        protocol::serializePacket(protocol::MsgType::CMD_SET_MODE, payload),
                        SharedState::LogCategory::command,
                        std::string("Mode: ") + protocol::flightModeToString(flightMode));
    }

    bool ProtocolClient::sendSimulationObstacles(const protocol::PayloadSimObstacles &obstacles)
    {
        return sendPacket(protocol::MsgType::CMD_SIM_OBSTACLES,
                        protocol::serializePacket(protocol::MsgType::CMD_SIM_OBSTACLES, obstacles),
                        SharedState::LogCategory::command,
                        "Obstacle grid updated");
    }

    bool ProtocolClient::sendSimulationLidar(bool lidarActive)
    {
        protocol::PayloadSimLidar payload{lidarActive};
        return sendPacket(protocol::MsgType::CMD_SIM_LIDAR,
                        protocol::serializePacket(protocol::MsgType::CMD_SIM_LIDAR, payload),
                        SharedState::LogCategory::command,
                        lidarActive ? "LiDAR simulation enabled" : "LiDAR simulation disabled");
    }

    void ProtocolClient::handleTcpPacket(std::vector<std::uint8_t> packetBytes)
    {
        const auto packetView = protocol::tryParsePacket(packetBytes);
        if (!packetView.has_value())
        {
            sharedState.appendLog(
                SharedState::LogDirection::inbound, SharedState::LogCategory::error, "TCP", "Invalid TCP packet");
            return;
        }

        switch (packetView->msgType)
        {
        case protocol::MsgType::TEL_STATE:
        {
            protocol::PayloadTelemetryState payload{};
            if (!protocol::parsePayload(packetView->payload, payload))
            {
                sharedState.appendLog(SharedState::LogDirection::inbound,
                                    SharedState::LogCategory::error,
                                    "TEL_STATE",
                                    "Malformed telemetry state payload");
                return;
            }

            sharedState.updateTelemetryState(payload);
            sharedState.appendLog(SharedState::LogDirection::inbound,
                                SharedState::LogCategory::telemetry,
                                "TEL_STATE",
                                buildStateSummary(payload));
            break;
        }
        case protocol::MsgType::TEL_ACK:
        {
            protocol::PayloadAck payload{};
            if (!protocol::parsePayload(packetView->payload, payload))
            {
                sharedState.appendLog(SharedState::LogDirection::inbound,
                                    SharedState::LogCategory::error,
                                    "TEL_ACK",
                                    "Malformed ACK payload");
                return;
            }

            sharedState.applyAck(payload);

            std::ostringstream stream;
            stream << protocol::ackResultToString(static_cast<protocol::AckResult>(payload.result));
            const auto message = protocol::ackMessageToString(payload);
            if (!message.empty())
            {
                stream << ": " << message;
            }

            sharedState.appendLog(
                SharedState::LogDirection::inbound, SharedState::LogCategory::command, "TEL_ACK", stream.str());
            break;
        }
        case protocol::MsgType::TEL_POSITION:
        {
            protocol::PayloadTelemetryPosition payload{};
            if (!protocol::parsePayload(packetView->payload, payload))
            {
                sharedState.appendLog(SharedState::LogDirection::inbound,
                                    SharedState::LogCategory::error,
                                    "TEL_POSITION",
                                    "Malformed position payload on TCP");
                return;
            }

            sharedState.updateTelemetryPosition(payload);
            sharedState.appendLog(SharedState::LogDirection::inbound,
                                SharedState::LogCategory::telemetry,
                                "TEL_POSITION",
                                buildPositionSummary(payload));
            break;
        }
        default:
            sharedState.appendLog(SharedState::LogDirection::inbound,
                                SharedState::LogCategory::error,
                                protocol::msgTypeToString(packetView->msgType),
                                "Unexpected TCP message type");
            break;
        }
    }

    void ProtocolClient::handleUdpPacket(std::vector<std::uint8_t> packetBytes)
    {
        const auto packetView = protocol::tryParsePacket(packetBytes);
        if (!packetView.has_value())
        {
            sharedState.appendLog(
                SharedState::LogDirection::inbound, SharedState::LogCategory::error, "UDP", "Invalid UDP packet");
            return;
        }

        switch (packetView->msgType)
        {
        case protocol::MsgType::TEL_POSITION:
        {
            protocol::PayloadTelemetryPosition payload{};
            if (!protocol::parsePayload(packetView->payload, payload))
            {
                sharedState.appendLog(SharedState::LogDirection::inbound,
                                    SharedState::LogCategory::error,
                                    "TEL_POSITION",
                                    "Malformed UDP position payload");
                return;
            }

            sharedState.updateTelemetryPosition(payload);
            sharedState.appendLog(SharedState::LogDirection::inbound,
                                SharedState::LogCategory::telemetry,
                                "TEL_POSITION",
                                buildPositionSummary(payload));
            break;
        }
        case protocol::MsgType::TEL_POINT_CLOUD:
        {
            if (packetView->payload.size() < sizeof(protocol::PayloadPointCloudHeader))
            {
                sharedState.appendLog(SharedState::LogDirection::inbound,
                                    SharedState::LogCategory::error,
                                    "TEL_POINT_CLOUD",
                                    "Point cloud payload shorter than header");
                return;
            }

            protocol::PayloadPointCloudHeader header{};
            std::memcpy(&header, packetView->payload.data(), sizeof(header));
            const std::size_t expectedPayloadSize = sizeof(protocol::PayloadPointCloudHeader) +
                                                    static_cast<std::size_t>(header.numPoints) *
                                                        sizeof(protocol::PointCloudPoint);
            if (header.numPoints > protocol::maxPointCloudPointsPerPacket || packetView->payload.size() != expectedPayloadSize)
            {
                sharedState.appendLog(SharedState::LogDirection::inbound,
                                    SharedState::LogCategory::error,
                                    "TEL_POINT_CLOUD",
                                    "Invalid point cloud frame size");
                return;
            }

            std::vector<protocol::PointCloudPoint> points(header.numPoints);
            if (!points.empty())
            {
                std::memcpy(points.data(),
                            packetView->payload.data() + sizeof(protocol::PayloadPointCloudHeader),
                            points.size() * sizeof(protocol::PointCloudPoint));
            }

            sharedState.updatePointCloud(header.timestampMs, std::move(points));
            sharedState.appendLog(SharedState::LogDirection::inbound,
                                SharedState::LogCategory::telemetry,
                                "TEL_POINT_CLOUD",
                                buildPointCloudSummary(header));
            break;
        }
        default:
            sharedState.appendLog(SharedState::LogDirection::inbound,
                                SharedState::LogCategory::error,
                                protocol::msgTypeToString(packetView->msgType),
                                "Unexpected UDP message type");
            break;
        }
    }

    void ProtocolClient::handleTcpStatus(SharedState::ConnectionStatus status, std::string message)
    {
        sharedState.setConnectionStatus(status);
        sharedState.appendLog(SharedState::LogDirection::local,
                            status == SharedState::ConnectionStatus::disconnected ? SharedState::LogCategory::error
                                                                                    : SharedState::LogCategory::telemetry,
                            "TCP",
                            std::move(message));
    }

    void ProtocolClient::handleUdpError(std::string message)
    {
        sharedState.appendLog(
            SharedState::LogDirection::local, SharedState::LogCategory::error, "UDP", std::move(message));
    }

    bool ProtocolClient::sendPacket(protocol::MsgType msgType,
                                    std::vector<std::uint8_t> packetBytes,
                                    SharedState::LogCategory logCategory,
                                    std::string description)
    {
        if (packetBytes.empty())
        {
            sharedState.appendLog(SharedState::LogDirection::local,
                                SharedState::LogCategory::error,
                                protocol::msgTypeToString(msgType),
                                "Failed to serialize packet");
            return false;
        }

        const bool queued = tcpClient.send(std::move(packetBytes));
        sharedState.appendLog(SharedState::LogDirection::outbound,
                            logCategory,
                            protocol::msgTypeToString(msgType),
                            std::move(description));
        if (!queued)
        {
            sharedState.appendLog(SharedState::LogDirection::local,
                                SharedState::LogCategory::error,
                                protocol::msgTypeToString(msgType),
                                "TCP thread is not running");
        }
        return queued;
    }
}