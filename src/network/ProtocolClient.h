#pragma once

#include "network/TcpClient.h"
#include "network/UdpReceiver.h"
#include "state/SharedState.h"

#include <string>

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
        void handleTcpPacket(std::vector<std::uint8_t> packetBytes);
        void handleUdpPacket(std::vector<std::uint8_t> packetBytes);
        void handleTcpStatus(SharedState::ConnectionStatus status, std::string message);
        void handleUdpError(std::string message);

        bool sendPacket(protocol::MsgType msgType,
                        std::vector<std::uint8_t> packetBytes,
                        SharedState::LogCategory logCategory,
                        std::string description);

        SharedState &sharedState;
        TcpClient tcpClient;
        UdpReceiver udpReceiver;
    };

}