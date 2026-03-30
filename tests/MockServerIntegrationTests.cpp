#include "network/ProtocolClient.h"
#include "state/SharedState.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
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

std::string quoteCommandArgument(const std::filesystem::path &value)
{
    return "\"" + value.string() + "\"";
}
} // namespace

int main()
{
    try
    {
        const char *serverBinaryRaw = std::getenv("ATOMGCS_TEST_SERVER_BIN");
        if (serverBinaryRaw == nullptr || std::string(serverBinaryRaw).empty())
        {
            std::cout << "Skipping integration test: ATOMGCS_TEST_SERVER_BIN is not set" << std::endl;
            return 0;
        }

        const std::filesystem::path serverBinary = std::filesystem::path(serverBinaryRaw);
        require(std::filesystem::exists(serverBinary), "configured server binary does not exist");

        const std::uint16_t tcpPort = 5860;
        const std::uint16_t udpPort = 5861;
        std::ostringstream commandStream;
        commandStream << quoteCommandArgument(serverBinary)
                      << " --tcp-port " << tcpPort
                      << " --udp-port " << udpPort
                      << " --runtime-sec 25";

        std::jthread serverThread([command = commandStream.str()]() {
            const int exitCode = std::system(command.c_str());
            if (exitCode != 0)
            {
                std::cerr << "External control module mock exited with code " << exitCode << std::endl;
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        gcs::SharedState sharedState;
        gcs::SharedState::ConnectionSettings connectionSettings;
        connectionSettings.ipAddress = "127.0.0.1";
        connectionSettings.tcpPort = tcpPort;
        connectionSettings.udpPort = udpPort;
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

        require(client.sendSimulationLidar(false), "failed to queue CMD_SIM_LIDAR off");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        gcs::protocol::PayloadSimObstacles obstacles{};
        obstacles.front = 1;
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