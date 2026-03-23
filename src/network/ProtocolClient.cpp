#include "network/ProtocolClient.h"

#include "shared/protocol/protocol_utils.h"

#include <cstring>
#include <numeric>
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

    std::string buildPointCloudSummary(std::uint32_t timestampMs, std::size_t pointCount)
    {
        std::ostringstream stream;
        stream << "Point cloud frame " << timestampMs << ": " << pointCount << " points";
        return stream.str();
    }
    } // namespace

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

        resetPartialPointCloud();
        udpReceiver.start(settings.udpPort);
        tcpClient.start(settings);
    }

    void ProtocolClient::disconnect()
    {
        tcpClient.stop();
        udpReceiver.stop();
        resetPartialPointCloud();
        sharedState.setConnectionStatus(SharedState::ConnectionStatus::disconnected);
        sharedState.appendLog(
            SharedState::LogDirection::local, SharedState::LogCategory::telemetry, "CONNECTION", "Disconnected");
    }

    bool ProtocolClient::sendCommand(protocol::CommandId commandId)
    {
        protocol::PayloadCommand payload{commandId};
        auto packet = protocol::serializePacket(protocol::MsgType::CMD_COMMAND, payload);

        if (!sendPacket(protocol::MsgType::CMD_COMMAND,
                        std::move(packet),
                        SharedState::LogCategory::command,
                        protocol::commandIdToString(commandId)))
        {
            return false;
        }

        sharedState.markCommandSent(commandId);
        return true;
    }

    bool ProtocolClient::sendMissionParameters(const SharedState::MissionParametersModel &missionParameters)
    {
        auto header = missionParameters.payload;
        header.numPoints = static_cast<std::uint32_t>(missionParameters.pointsNed.size());
        const auto payloadBytes = protocol::serializeMissionPayload(header, missionParameters.pointsNed);
        return sendPacket(protocol::MsgType::CMD_SET_MISSION,
                        protocol::serializePacket(protocol::MsgType::CMD_SET_MISSION,
                                                    std::span<const std::uint8_t>(payloadBytes.data(), payloadBytes.size())),
                        SharedState::LogCategory::command,
                        "Mission payload: " + std::to_string(header.numPoints) + " NED points");
    }

    bool ProtocolClient::sendMode(protocol::FlightMode flightMode)
    {
        protocol::PayloadSetMode payload{flightMode};
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
        protocol::PayloadSimLidar payload{static_cast<std::uint8_t>(lidarActive ? 1 : 0)};
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
            break;
        }
        case protocol::MsgType::TEL_POINT_CLOUD:
        {
            protocol::PayloadPointCloudPacketHeader header{};
            std::vector<protocol::PointCloudPoint> points;
            if (!protocol::parsePointCloudPayload(packetView->payload, header, points))
            {
                sharedState.appendLog(SharedState::LogDirection::inbound,
                                    SharedState::LogCategory::error,
                                    "TEL_POINT_CLOUD",
                                    "Invalid point cloud packet payload");
                return;
            }

            handlePointCloudPacket(header, std::move(points));
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
        if (status == SharedState::ConnectionStatus::disconnected)
        {
            resetPartialPointCloud();
        }

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

    void ProtocolClient::handlePointCloudPacket(const protocol::PayloadPointCloudPacketHeader &header,
                                                std::vector<protocol::PointCloudPoint> points)
    {
        const auto now = std::chrono::steady_clock::now();
        const bool shouldStartNewFrame = !partialPointCloud.has_value() ||
                                        partialPointCloud->timestampMs != header.frameTimestampMs ||
                                        partialPointCloud->packetCount != header.packetCount ||
                                        partialPointCloud->totalPoints != header.totalPoints ||
                                        (now - partialPointCloud->startedAt) > std::chrono::milliseconds(400);

        if (shouldStartNewFrame)
        {
            publishPartialPointCloud();
            partialPointCloud = PartialPointCloudFrame{};
            partialPointCloud->timestampMs = header.frameTimestampMs;
            partialPointCloud->packetCount = header.packetCount;
            partialPointCloud->totalPoints = header.totalPoints;
            partialPointCloud->receivedPackets = 0;
            partialPointCloud->packetPoints.resize(header.packetCount);
            partialPointCloud->received.assign(header.packetCount, false);
            partialPointCloud->startedAt = now;
        }

        if (!partialPointCloud->received[header.packetIndex])
        {
            partialPointCloud->packetPoints[header.packetIndex] = std::move(points);
            partialPointCloud->received[header.packetIndex] = true;
            partialPointCloud->receivedPackets += 1;
        }

        if (partialPointCloud->receivedPackets == partialPointCloud->packetCount)
        {
            publishPartialPointCloud();
        }
    }

    void ProtocolClient::publishPartialPointCloud()
    {
        if (!partialPointCloud.has_value() || partialPointCloud->receivedPackets == 0)
        {
            resetPartialPointCloud();
            return;
        }

        std::vector<protocol::PointCloudPoint> framePoints;
        framePoints.reserve(partialPointCloud->totalPoints);
        for (const auto &packetPoints : partialPointCloud->packetPoints)
        {
            framePoints.insert(framePoints.end(), packetPoints.begin(), packetPoints.end());
        }

        if (framePoints.size() > partialPointCloud->totalPoints)
        {
            framePoints.resize(partialPointCloud->totalPoints);
        }

        const auto finalPointCount = framePoints.size();
        sharedState.updatePointCloud(partialPointCloud->timestampMs, std::move(framePoints));
        sharedState.appendLog(SharedState::LogDirection::inbound,
                            SharedState::LogCategory::telemetry,
                            "TEL_POINT_CLOUD",
                            buildPointCloudSummary(partialPointCloud->timestampMs, finalPointCount));
        resetPartialPointCloud();
    }

    void ProtocolClient::resetPartialPointCloud()
    {
        partialPointCloud.reset();
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

} // namespace gcs::network