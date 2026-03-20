#pragma once

#include "shared/protocol/protocol.h"

#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gcs::mock
{

    struct MockServerConfig
    {
        std::uint16_t tcpPort = 5760;
        std::uint16_t udpPort = 5761;
        std::uint32_t telemetryHz = 10;
        std::optional<std::chrono::seconds> runtimeLimit;
    };

    class MockServer
    {
    public:
        explicit MockServer(MockServerConfig config);
        ~MockServer();

        MockServer(const MockServer &) = delete;
        MockServer &operator=(const MockServer &) = delete;

        int run();
        void requestStop();

    private:
        struct TimedTransition
        {
            protocol::DroneState nextState = protocol::DroneState::DISCONNECTED;
            std::chrono::steady_clock::time_point executeAt{};
        };

        struct RuntimeState
        {
            bool clientConnected = false;
            protocol::DroneState droneState = protocol::DroneState::DISCONNECTED;
            protocol::FlightMode flightMode = protocol::FlightMode::AUTOMATIC;
            protocol::PayloadMissionParams missionParameters{};
            protocol::PayloadSimObstacles obstacles{};
            bool lidarActive = true;
            bool frontObstacleAutoPauseLatched = false;
            std::uint8_t batteryPercent = 92;
            float posX = 0.0f;
            float posY = 0.0f;
            float posZ = 0.0f;
            float velX = 0.0f;
            float velY = 0.0f;
            float velZ = 0.0f;
            float headingDeg = 0.0f;
            float altitudeAglM = 0.0f;
            float missionPhase = 0.0f;
            std::optional<TimedTransition> pendingTransition;
            std::chrono::steady_clock::time_point lastSimulationTick{};
            std::optional<asio::ip::udp::endpoint> udpTargetEndpoint;
        };

        struct TelemetrySnapshot
        {
            bool clientConnected = false;
            bool lidarActive = true;
            protocol::PayloadTelemetryPosition position{};
            std::optional<asio::ip::udp::endpoint> udpTargetEndpoint;
        };

        bool shouldStop() const;
        void acceptLoop();
        void telemetryLoop(std::stop_token stopToken);

        void onClientConnected(const asio::ip::tcp::endpoint &remoteEndpoint);
        void onClientDisconnected();
        bool tickStateMachine();
        void updateSimulationLocked(std::chrono::steady_clock::time_point now, RuntimeState &state) const;
        void transitionToLocked(RuntimeState &state,
                                protocol::DroneState newState,
                                std::chrono::steady_clock::time_point now,
                                std::string_view reason);
        std::uint32_t availableCommandsForState(protocol::DroneState state) const;
        protocol::PayloadTelemetryState makeTelemetryStateLocked(const RuntimeState &state) const;
        TelemetrySnapshot makeTelemetrySnapshot() const;

        void processTcpPacket(asio::ip::tcp::socket &socket, std::span<const std::uint8_t> packetBytes);
        void handleCommand(asio::ip::tcp::socket &socket, const protocol::PayloadCommand &payload);
        void handleMissionParameters(asio::ip::tcp::socket &socket, const protocol::PayloadMissionParams &payload);
        void handleMode(asio::ip::tcp::socket &socket, const protocol::PayloadSetMode &payload);
        void handleObstacles(asio::ip::tcp::socket &socket, const protocol::PayloadSimObstacles &payload);
        void handleLidar(asio::ip::tcp::socket &socket, const protocol::PayloadSimLidar &payload);

        void sendStatePacket(asio::ip::tcp::socket &socket);
        void sendAck(asio::ip::tcp::socket &socket,
                    protocol::MsgType originalMsgType,
                    std::uint8_t originalCommandId,
                    protocol::AckResult result,
                    std::string_view message);
        void sendPositionUdp(const TelemetrySnapshot &snapshot, asio::ip::udp::socket &udpSocket);
        void sendPointCloudUdp(const TelemetrySnapshot &snapshot, asio::ip::udp::socket &udpSocket);

        void log(std::string_view message) const;

        MockServerConfig config;
        mutable std::mutex stateMutex;
        RuntimeState state;
        std::atomic_bool stopRequested{false};
        std::jthread telemetryThread;
        std::chrono::steady_clock::time_point startedAt{};
    };

    MockServerConfig parseMockServerConfig(int argc, char **argv);

}