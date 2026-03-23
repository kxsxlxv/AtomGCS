#pragma once

#include "network/TcpClient.h"
#include "network/UdpReceiver.h"
#include "state/SharedState.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace gcs::network
{

    class ProtocolClient
    {
    public:
        explicit ProtocolClient(SharedState &sharedState);
        ~ProtocolClient() = default;

        ProtocolClient(const ProtocolClient &) = delete;
        ProtocolClient &operator=(const ProtocolClient &) = delete;

        void connect();
        void disconnect();

        bool sendCommand(protocol::CommandId commandId);
        bool sendMissionParameters(const SharedState::MissionParametersModel &missionParameters);
        bool sendMode(protocol::FlightMode flightMode);
        bool sendSimulationObstacles(const protocol::PayloadSimObstacles &obstacles);
        bool sendSimulationLidar(bool lidarActive);

    private:
        struct PartialPointCloudFrame
        {
            std::uint32_t timestampMs = 0;
            std::uint16_t packetCount = 0;
            std::uint16_t totalPoints = 0;
            std::uint16_t receivedPackets = 0;
            std::vector<std::vector<protocol::PointCloudPoint>> packetPoints;
            std::vector<bool> received;
            std::chrono::steady_clock::time_point startedAt{};
        };

        void handleTcpPacket(std::vector<std::uint8_t> packetBytes);
        void handleUdpPacket(std::vector<std::uint8_t> packetBytes);
        void handleTcpStatus(SharedState::ConnectionStatus status, std::string message);
        void handleUdpError(std::string message);
        void handlePointCloudPacket(const protocol::PayloadPointCloudPacketHeader &header,
                                    std::vector<protocol::PointCloudPoint> points);
        void publishPartialPointCloud();
        void resetPartialPointCloud();

        bool sendPacket(protocol::MsgType msgType,
                        std::vector<std::uint8_t> packetBytes,
                        SharedState::LogCategory logCategory,
                        std::string description);

        SharedState &sharedState;
        TcpClient tcpClient;
        UdpReceiver udpReceiver;
        std::optional<PartialPointCloudFrame> partialPointCloud;
    };

} // namespace gcs::network