#include "mock/MockServer.h"
#include "network/ProtocolClient.h"
#include "state/SharedState.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Predicate>
void waitUntil(Predicate predicate, std::chrono::milliseconds timeout, const char *message)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout)
    {
        if (predicate())
        {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    throw std::runtime_error(message);
}
} // namespace

int main()
{
    try
    {
        gcs::mock::MockServerConfig serverConfig;
        serverConfig.tcpPort = 5860;
        serverConfig.udpPort = 5861;
        serverConfig.runtimeLimit = std::chrono::seconds(30);

        gcs::mock::MockServer server(serverConfig);
        std::jthread serverThread([&server]() { server.run(); });

        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        gcs::SharedState sharedState;
        gcs::SharedState::ConnectionSettings connectionSettings;
        connectionSettings.ipAddress = "127.0.0.1";
        connectionSettings.tcpPort = serverConfig.tcpPort;
        connectionSettings.udpPort = serverConfig.udpPort;
        sharedState.setConnectionSettings(connectionSettings);

        gcs::network::ProtocolClient client(sharedState);
        client.connect();

        waitUntil(
            [&]() {
                const auto snapshot = sharedState.snapshot();
                return snapshot.connectionStatus == gcs::SharedState::ConnectionStatus::connected &&
                       static_cast<gcs::protocol::DroneState>(snapshot.telemetryState.currentState) ==
                           gcs::protocol::DroneState::IDLE;
            },
            std::chrono::seconds(3),
            "client failed to reach IDLE state");

        require(client.sendCommand(gcs::protocol::CommandId::PREPARE), "failed to queue PREPARE command");
        waitUntil(
            [&]() {
                return static_cast<gcs::protocol::DroneState>(sharedState.getTelemetryState().currentState) ==
                       gcs::protocol::DroneState::READY;
            },
            std::chrono::seconds(5),
            "mock server did not transition to READY");

        require(client.sendCommand(gcs::protocol::CommandId::TAKEOFF), "failed to queue TAKEOFF command");
        waitUntil(
            [&]() {
                return static_cast<gcs::protocol::DroneState>(sharedState.getTelemetryState().currentState) ==
                       gcs::protocol::DroneState::IN_FLIGHT;
            },
            std::chrono::seconds(10),
            "mock server did not transition to IN_FLIGHT");

        require(client.sendCommand(gcs::protocol::CommandId::START_MISSION),
                "failed to queue START_MISSION command");
        waitUntil(
            [&]() {
                return static_cast<gcs::protocol::DroneState>(sharedState.getTelemetryState().currentState) ==
                       gcs::protocol::DroneState::EXECUTING_MISSION;
            },
            std::chrono::seconds(3),
            "mock server did not transition to EXECUTING_MISSION");

        waitUntil([&]() { return sharedState.getPointCloud().revision > 0; },
                  std::chrono::seconds(3),
                  "mock server did not publish point cloud");
        const auto pointCloudRevisionBeforeLidarOff = sharedState.getPointCloud().revision;

        require(client.sendSimulationLidar(false), "failed to queue CMD_SIM_LIDAR off");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const auto pointCloudRevisionAfterLidarOff = sharedState.getPointCloud().revision;
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        require(sharedState.getPointCloud().revision == pointCloudRevisionAfterLidarOff,
                "point cloud kept changing after LiDAR was disabled");

        gcs::protocol::PayloadSimObstacles obstacles{};
        obstacles.front = true;
        require(client.sendSimulationObstacles(obstacles), "failed to queue CMD_SIM_OBSTACLES");
        waitUntil(
            [&]() {
                return static_cast<gcs::protocol::DroneState>(sharedState.getTelemetryState().currentState) ==
                       gcs::protocol::DroneState::PAUSED;
            },
            std::chrono::seconds(3),
            "front obstacle did not auto-pause mission");

        const auto position = sharedState.getTelemetryPosition();
        require(position.altitudeAglM > 0.1f, "telemetry altitude did not update");

        client.disconnect();
        server.requestStop();
        serverThread.join();

        std::cout << "Mock server integration test passed" << std::endl;
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Mock server integration test failed: " << exception.what() << std::endl;
        return 1;
    }
}

