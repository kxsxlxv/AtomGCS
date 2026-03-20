#include "mock/MockServer.h"

#include "shared/protocol/protocol_utils.h"

#include <asio/io_context.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace gcs::mock
{

    namespace
    {
        constexpr auto sessionPollInterval = std::chrono::milliseconds(20);
        constexpr float pi = 3.14159265358979323846f;

        std::string stateName(protocol::DroneState state)
        {
            return protocol::droneStateToString(state);
        }

        std::vector<std::uint8_t> buildPointCloudPayload(std::uint32_t timestampMs)
        {
            static thread_local std::mt19937 randomEngine{std::random_device{}()};
            std::uniform_int_distribution<std::uint32_t> pointCountDistribution(1000, 5000);
            std::uniform_real_distribution<float> radiusDistribution(2.0f, 25.0f);
            std::uniform_real_distribution<float> azimuthDistribution(-pi * 0.5f, pi * 0.5f);
            std::uniform_real_distribution<float> elevationDistribution(-pi * 0.35f, pi * 0.35f);
            std::uniform_int_distribution<int> intensityDistribution(20, 255);

            const std::uint32_t pointCount = pointCountDistribution(randomEngine);
            protocol::PayloadPointCloudHeader header{timestampMs, pointCount};

            std::vector<std::uint8_t> payload(sizeof(header) + static_cast<std::size_t>(pointCount) * sizeof(protocol::PointCloudPoint));
            std::memcpy(payload.data(), &header, sizeof(header));

            auto *points = reinterpret_cast<protocol::PointCloudPoint *>(payload.data() + sizeof(header));
            for (std::uint32_t index = 0; index < pointCount; ++index)
            {
                const float radius = radiusDistribution(randomEngine);
                const float azimuth = azimuthDistribution(randomEngine);
                const float elevation = elevationDistribution(randomEngine);

                points[index].x = radius * std::cos(elevation) * std::cos(azimuth);
                points[index].y = radius * std::cos(elevation) * std::sin(azimuth);
                points[index].z = radius * std::sin(elevation);
                points[index].intensity = static_cast<std::uint8_t>(intensityDistribution(randomEngine));
            }

            return payload;
        }
    }

    MockServer::MockServer(MockServerConfig configValue)
        : config(std::move(configValue))
    {
        state.missionParameters.delayedStartTimeSec = 0;
        state.missionParameters.takeoffAltitudeM = 10.0f;
        state.missionParameters.flightSpeedMS = 5.0f;
    }

    MockServer::~MockServer()
    {
        requestStop();
        if (telemetryThread.joinable())
        {
            telemetryThread.request_stop();
            telemetryThread.join();
        }
    }

    int MockServer::run()
    {
        startedAt = std::chrono::steady_clock::now();
        log("mock_server starting on TCP " + std::to_string(config.tcpPort) + ", UDP " + std::to_string(config.udpPort));

        telemetryThread = std::jthread([this](std::stop_token stopToken) { telemetryLoop(stopToken); });
        acceptLoop();

        if (telemetryThread.joinable())
        {
            telemetryThread.request_stop();
            telemetryThread.join();
        }

        log("mock_server stopped");
        return 0;
    }

    void MockServer::requestStop()
    {
        stopRequested.store(true);
    }

    bool MockServer::shouldStop() const
    {
        if (stopRequested.load())
        {
            return true;
        }

        if (config.runtimeLimit.has_value())
        {
            return std::chrono::steady_clock::now() - startedAt >= *config.runtimeLimit;
        }

        return false;
    }

    void MockServer::acceptLoop()
    {
        asio::io_context ioContext;
        asio::ip::tcp::acceptor acceptor(ioContext);

        std::error_code errorCode;
        acceptor.open(asio::ip::tcp::v4(), errorCode);
        if (errorCode)
        {
            log("acceptor open failed: " + errorCode.message());
            return;
        }

        acceptor.set_option(asio::socket_base::reuse_address(true), errorCode);
        acceptor.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), config.tcpPort), errorCode);
        if (errorCode)
        {
            log("acceptor bind failed: " + errorCode.message());
            return;
        }

        acceptor.listen(asio::socket_base::max_listen_connections, errorCode);
        if (errorCode)
        {
            log("acceptor listen failed: " + errorCode.message());
            return;
        }

        acceptor.non_blocking(true, errorCode);

        while (!shouldStop())
        {
            asio::ip::tcp::socket socket(ioContext);
            acceptor.accept(socket, errorCode);
            if (errorCode == asio::error::would_block || errorCode == asio::error::try_again)
            {
                std::this_thread::sleep_for(sessionPollInterval);
                continue;
            }
            if (errorCode)
            {
                log("accept failed: " + errorCode.message());
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                continue;
            }

            socket.non_blocking(true, errorCode);
            const auto remoteEndpoint = socket.remote_endpoint(errorCode);
            if (errorCode)
            {
                log("failed to resolve remote endpoint: " + errorCode.message());
                continue;
            }

            onClientConnected(remoteEndpoint);
            sendStatePacket(socket);

            protocol::PacketStreamParser streamParser;
            std::array<std::uint8_t, 4096> readBuffer{};

            while (!shouldStop())
            {
                if (tickStateMachine())
                {
                    sendStatePacket(socket);
                }

                const std::size_t bytesRead = socket.read_some(asio::buffer(readBuffer), errorCode);
                if (!errorCode)
                {
                    streamParser.append(std::span<const std::uint8_t>(readBuffer.data(), bytesRead));
                    for (auto &packetBytes : streamParser.extractPackets())
                    {
                        processTcpPacket(socket, packetBytes);
                    }
                    continue;
                }

                if (errorCode == asio::error::would_block || errorCode == asio::error::try_again)
                {
                    std::this_thread::sleep_for(sessionPollInterval);
                    continue;
                }

                if (errorCode == asio::error::eof || errorCode == asio::error::connection_reset)
                {
                    log("client disconnected");
                }
                else
                {
                    log("socket read failed: " + errorCode.message());
                }
                break;
            }

            std::error_code ignoredError;
            socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignoredError);
            socket.close(ignoredError);
            onClientDisconnected();
        }
    }

    void MockServer::telemetryLoop(std::stop_token stopToken)
    {
        asio::io_context ioContext;
        asio::ip::udp::socket udpSocket(ioContext);

        std::error_code errorCode;
        udpSocket.open(asio::ip::udp::v4(), errorCode);
        if (errorCode)
        {
            log("UDP open failed: " + errorCode.message());
            return;
        }

        const auto interval = std::chrono::milliseconds(std::max<std::uint32_t>(1, 1000U / std::max<std::uint32_t>(1, config.telemetryHz)));

        while (!stopToken.stop_requested() && !shouldStop())
        {
            const auto snapshot = makeTelemetrySnapshot();
            if (snapshot.clientConnected && snapshot.udpTargetEndpoint.has_value())
            {
                sendPositionUdp(snapshot, udpSocket);
                if (snapshot.lidarActive)
                {
                    sendPointCloudUdp(snapshot, udpSocket);
                }
            }

            std::this_thread::sleep_for(interval);
        }

        udpSocket.close(errorCode);
    }

    void MockServer::onClientConnected(const asio::ip::tcp::endpoint &remoteEndpoint)
    {
        std::scoped_lock lock(stateMutex);
        state.clientConnected = true;
        state.udpTargetEndpoint = asio::ip::udp::endpoint(remoteEndpoint.address(), config.udpPort);
        state.lastSimulationTick = std::chrono::steady_clock::now();
        transitionToLocked(state, protocol::DroneState::CONNECTED, state.lastSimulationTick, "TCP client connected");
    }

    void MockServer::onClientDisconnected()
    {
        std::scoped_lock lock(stateMutex);
        state.clientConnected = false;
        state.udpTargetEndpoint.reset();
        state.pendingTransition.reset();
        state.droneState = protocol::DroneState::DISCONNECTED;
        state.velX = 0.0f;
        state.velY = 0.0f;
        state.velZ = 0.0f;
        state.altitudeAglM = std::max(0.0f, state.posZ);
    }

    bool MockServer::tickStateMachine()
    {
        const auto now = std::chrono::steady_clock::now();
        std::scoped_lock lock(stateMutex);

        bool changed = false;
        updateSimulationLocked(now, state);

        if (state.pendingTransition.has_value() && now >= state.pendingTransition->executeAt)
        {
            const auto nextState = state.pendingTransition->nextState;
            state.pendingTransition.reset();
            transitionToLocked(state, nextState, now, "timer transition");
            changed = true;
        }

        if (!state.obstacles.front)
        {
            state.frontObstacleAutoPauseLatched = false;
        }
        else if (state.droneState == protocol::DroneState::EXECUTING_MISSION && !state.frontObstacleAutoPauseLatched)
        {
            state.frontObstacleAutoPauseLatched = true;
            transitionToLocked(state, protocol::DroneState::PAUSED, now, "front obstacle auto-pause");
            changed = true;
        }

        return changed;
    }

    void MockServer::updateSimulationLocked(std::chrono::steady_clock::time_point now, RuntimeState &runtimeState) const
    {
        if (runtimeState.lastSimulationTick.time_since_epoch().count() == 0)
        {
            runtimeState.lastSimulationTick = now;
            return;
        }

        const float deltaSeconds = std::chrono::duration<float>(now - runtimeState.lastSimulationTick).count();
        runtimeState.lastSimulationTick = now;
        runtimeState.headingDeg = std::fmod(runtimeState.headingDeg + deltaSeconds * 18.0f, 360.0f);

        const float targetAltitude = std::max(runtimeState.missionParameters.takeoffAltitudeM, 1.0f);
        const float flightSpeed = std::max(runtimeState.missionParameters.flightSpeedMS, 0.5f);

        float previousX = runtimeState.posX;
        float previousY = runtimeState.posY;
        float previousZ = runtimeState.posZ;

        switch (runtimeState.droneState)
        {
        case protocol::DroneState::TAKING_OFF:
            runtimeState.posZ = std::min(targetAltitude, runtimeState.posZ + targetAltitude * 0.2f * deltaSeconds);
            break;
        case protocol::DroneState::IN_FLIGHT:
            runtimeState.posZ = targetAltitude;
            break;
        case protocol::DroneState::EXECUTING_MISSION:
        {
            runtimeState.missionPhase += deltaSeconds * flightSpeed * 0.25f;
            runtimeState.posX = 10.0f * std::cos(runtimeState.missionPhase);
            runtimeState.posY = 10.0f * std::sin(runtimeState.missionPhase);
            runtimeState.posZ = targetAltitude;
            break;
        }
        case protocol::DroneState::PAUSED:
            runtimeState.posZ = targetAltitude;
            break;
        case protocol::DroneState::RETURNING_HOME:
        {
            const float homeX = 0.0f;
            const float homeY = 0.0f;
            const float deltaX = homeX - runtimeState.posX;
            const float deltaY = homeY - runtimeState.posY;
            const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
            if (distance > 0.001f)
            {
                const float step = std::min(distance, flightSpeed * deltaSeconds);
                runtimeState.posX += deltaX / distance * step;
                runtimeState.posY += deltaY / distance * step;
            }
            runtimeState.posZ = targetAltitude;
            break;
        }
        case protocol::DroneState::LANDING:
            runtimeState.posZ = std::max(0.0f, runtimeState.posZ - std::max(1.0f, targetAltitude * 0.2f) * deltaSeconds);
            break;
        case protocol::DroneState::LANDED:
        case protocol::DroneState::IDLE:
        case protocol::DroneState::CONNECTED:
        case protocol::DroneState::PREPARING:
        case protocol::DroneState::READY:
        case protocol::DroneState::ARMING:
        case protocol::DroneState::ERROR:
        case protocol::DroneState::EMERGENCY_LANDING:
        case protocol::DroneState::DISCONNECTED:
        default:
            runtimeState.posZ = std::max(0.0f, runtimeState.posZ - 2.0f * deltaSeconds);
            break;
        }

        runtimeState.velX = deltaSeconds > 0.0f ? (runtimeState.posX - previousX) / deltaSeconds : 0.0f;
        runtimeState.velY = deltaSeconds > 0.0f ? (runtimeState.posY - previousY) / deltaSeconds : 0.0f;
        runtimeState.velZ = deltaSeconds > 0.0f ? (runtimeState.posZ - previousZ) / deltaSeconds : 0.0f;
        runtimeState.altitudeAglM = runtimeState.posZ;
    }

    void MockServer::transitionToLocked(RuntimeState &runtimeState,
                                        protocol::DroneState newState,
                                        std::chrono::steady_clock::time_point now,
                                        std::string_view reason)
    {
        runtimeState.droneState = newState;
        runtimeState.pendingTransition.reset();

        switch (newState)
        {
        case protocol::DroneState::CONNECTED:
            runtimeState.pendingTransition = TimedTransition{protocol::DroneState::IDLE, now + std::chrono::milliseconds(500)};
            break;
        case protocol::DroneState::PREPARING:
            runtimeState.pendingTransition = TimedTransition{protocol::DroneState::READY, now + std::chrono::seconds(3)};
            break;
        case protocol::DroneState::ARMING:
            runtimeState.pendingTransition = TimedTransition{protocol::DroneState::TAKING_OFF, now + std::chrono::seconds(2)};
            break;
        case protocol::DroneState::TAKING_OFF:
            runtimeState.pendingTransition = TimedTransition{protocol::DroneState::IN_FLIGHT, now + std::chrono::seconds(5)};
            break;
        case protocol::DroneState::RETURNING_HOME:
            runtimeState.pendingTransition = TimedTransition{protocol::DroneState::LANDING, now + std::chrono::seconds(10)};
            break;
        case protocol::DroneState::LANDING:
            runtimeState.pendingTransition = TimedTransition{protocol::DroneState::LANDED, now + std::chrono::seconds(5)};
            break;
        case protocol::DroneState::LANDED:
            runtimeState.posZ = 0.0f;
            runtimeState.altitudeAglM = 0.0f;
            runtimeState.velX = 0.0f;
            runtimeState.velY = 0.0f;
            runtimeState.velZ = 0.0f;
            break;
        default:
            break;
        }

        log(std::string("state -> ") + stateName(newState) + " (" + std::string(reason) + ")");
    }

    std::uint32_t MockServer::availableCommandsForState(protocol::DroneState droneState) const
    {
        using protocol::CommandId;
        std::uint32_t mask = 0;

        switch (droneState)
        {
        case protocol::DroneState::IDLE:
        case protocol::DroneState::LANDED:
        case protocol::DroneState::ERROR:
            mask |= protocol::commandBit(CommandId::PREPARE);
            break;
        case protocol::DroneState::READY:
            mask |= protocol::commandBit(CommandId::TAKEOFF);
            break;
        case protocol::DroneState::IN_FLIGHT:
            mask |= protocol::commandBit(CommandId::START_MISSION);
            mask |= protocol::commandBit(CommandId::RETURN_HOME);
            mask |= protocol::commandBit(CommandId::LAND);
            break;
        case protocol::DroneState::EXECUTING_MISSION:
            mask |= protocol::commandBit(CommandId::PAUSE_RESUME);
            mask |= protocol::commandBit(CommandId::RETURN_HOME);
            mask |= protocol::commandBit(CommandId::LAND);
            break;
        case protocol::DroneState::PAUSED:
            mask |= protocol::commandBit(CommandId::PAUSE_RESUME);
            mask |= protocol::commandBit(CommandId::RETURN_HOME);
            mask |= protocol::commandBit(CommandId::LAND);
            break;
        default:
            break;
        }

        if (droneState != protocol::DroneState::DISCONNECTED && droneState != protocol::DroneState::CONNECTED)
        {
            mask |= protocol::commandBit(CommandId::EMERGENCY_STOP);
        }

        return mask;
    }

    protocol::PayloadTelemetryState MockServer::makeTelemetryStateLocked(const RuntimeState &runtimeState) const
    {
        protocol::PayloadTelemetryState telemetryState{};
        telemetryState.currentState = static_cast<std::uint8_t>(runtimeState.droneState);
        telemetryState.availableCommands = availableCommandsForState(runtimeState.droneState);
        telemetryState.flightMode = static_cast<std::uint8_t>(runtimeState.flightMode);
        telemetryState.batteryPercent = runtimeState.batteryPercent;
        return telemetryState;
    }

    MockServer::TelemetrySnapshot MockServer::makeTelemetrySnapshot() const
    {
        std::scoped_lock lock(stateMutex);

        TelemetrySnapshot snapshot;
        snapshot.clientConnected = state.clientConnected;
        snapshot.lidarActive = state.lidarActive;
        snapshot.udpTargetEndpoint = state.udpTargetEndpoint;
        snapshot.position.posX = state.posX;
        snapshot.position.posY = state.posY;
        snapshot.position.posZ = state.posZ;
        snapshot.position.velX = state.velX;
        snapshot.position.velY = state.velY;
        snapshot.position.velZ = state.velZ;
        snapshot.position.headingDeg = state.headingDeg;
        snapshot.position.altitudeAglM = state.altitudeAglM;
        return snapshot;
    }

    void MockServer::processTcpPacket(asio::ip::tcp::socket &socket, std::span<const std::uint8_t> packetBytes)
    {
        const auto packetView = protocol::tryParsePacket(packetBytes);
        if (!packetView.has_value())
        {
            log("received invalid TCP packet");
            return;
        }

        switch (packetView->msgType)
        {
        case protocol::MsgType::CMD_COMMAND:
        {
            protocol::PayloadCommand payload{};
            if (!protocol::parsePayload(packetView->payload, payload))
            {
                sendAck(socket, packetView->msgType, 0, protocol::AckResult::INVALID_PARAM, "invalid command payload");
                return;
            }
            handleCommand(socket, payload);
            break;
        }
        case protocol::MsgType::CMD_SET_PARAMS:
        {
            protocol::PayloadMissionParams payload{};
            if (!protocol::parsePayload(packetView->payload, payload))
            {
                sendAck(socket, packetView->msgType, 0, protocol::AckResult::INVALID_PARAM, "invalid params payload");
                return;
            }
            handleMissionParameters(socket, payload);
            break;
        }
        case protocol::MsgType::CMD_SET_MODE:
        {
            protocol::PayloadSetMode payload{};
            if (!protocol::parsePayload(packetView->payload, payload))
            {
                sendAck(socket, packetView->msgType, 0, protocol::AckResult::INVALID_PARAM, "invalid mode payload");
                return;
            }
            handleMode(socket, payload);
            break;
        }
        case protocol::MsgType::CMD_SIM_OBSTACLES:
        {
            protocol::PayloadSimObstacles payload{};
            if (!protocol::parsePayload(packetView->payload, payload))
            {
                sendAck(socket, packetView->msgType, 0, protocol::AckResult::INVALID_PARAM, "invalid obstacle payload");
                return;
            }
            handleObstacles(socket, payload);
            break;
        }
        case protocol::MsgType::CMD_SIM_LIDAR:
        {
            protocol::PayloadSimLidar payload{};
            if (!protocol::parsePayload(packetView->payload, payload))
            {
                sendAck(socket, packetView->msgType, 0, protocol::AckResult::INVALID_PARAM, "invalid lidar payload");
                return;
            }
            handleLidar(socket, payload);
            break;
        }
        default:
            sendAck(socket, packetView->msgType, 0, protocol::AckResult::REJECTED, "unsupported message type");
            break;
        }
    }

    void MockServer::handleCommand(asio::ip::tcp::socket &socket, const protocol::PayloadCommand &payload)
    {
        const auto commandId = static_cast<protocol::CommandId>(payload.commandId);
        log(std::string("command: ") + protocol::commandIdToString(commandId));

        const auto now = std::chrono::steady_clock::now();
        std::string rejectReason;
        bool success = false;
        bool sendState = false;

        {
            std::scoped_lock lock(stateMutex);
            switch (commandId)
            {
            case protocol::CommandId::PREPARE:
                if (state.droneState == protocol::DroneState::IDLE || state.droneState == protocol::DroneState::LANDED ||
                    state.droneState == protocol::DroneState::ERROR)
                {
                    transitionToLocked(state, protocol::DroneState::PREPARING, now, "CMD_PREPARE");
                    success = true;
                    sendState = true;
                }
                else
                {
                    rejectReason = "PREPARE not allowed in current state";
                }
                break;
            case protocol::CommandId::TAKEOFF:
                if (state.droneState == protocol::DroneState::READY)
                {
                    transitionToLocked(state, protocol::DroneState::ARMING, now, "CMD_TAKEOFF");
                    success = true;
                    sendState = true;
                }
                else
                {
                    rejectReason = "TAKEOFF requires READY";
                }
                break;
            case protocol::CommandId::START_MISSION:
                if (state.droneState == protocol::DroneState::IN_FLIGHT)
                {
                    transitionToLocked(state, protocol::DroneState::EXECUTING_MISSION, now, "CMD_START_MISSION");
                    success = true;
                    sendState = true;
                }
                else
                {
                    rejectReason = "START_MISSION requires IN_FLIGHT";
                }
                break;
            case protocol::CommandId::PAUSE_RESUME:
                if (state.droneState == protocol::DroneState::EXECUTING_MISSION)
                {
                    transitionToLocked(state, protocol::DroneState::PAUSED, now, "CMD_PAUSE");
                    success = true;
                    sendState = true;
                }
                else if (state.droneState == protocol::DroneState::PAUSED)
                {
                    transitionToLocked(state, protocol::DroneState::EXECUTING_MISSION, now, "CMD_RESUME");
                    success = true;
                    sendState = true;
                }
                else
                {
                    rejectReason = "PAUSE_RESUME requires EXECUTING_MISSION or PAUSED";
                }
                break;
            case protocol::CommandId::RETURN_HOME:
                if (state.droneState == protocol::DroneState::IN_FLIGHT ||
                    state.droneState == protocol::DroneState::EXECUTING_MISSION ||
                    state.droneState == protocol::DroneState::PAUSED)
                {
                    transitionToLocked(state, protocol::DroneState::RETURNING_HOME, now, "CMD_RETURN_HOME");
                    success = true;
                    sendState = true;
                }
                else
                {
                    rejectReason = "RETURN_HOME requires airborne state";
                }
                break;
            case protocol::CommandId::LAND:
                if (state.droneState == protocol::DroneState::IN_FLIGHT ||
                    state.droneState == protocol::DroneState::EXECUTING_MISSION ||
                    state.droneState == protocol::DroneState::PAUSED)
                {
                    transitionToLocked(state, protocol::DroneState::LANDING, now, "CMD_LAND");
                    success = true;
                    sendState = true;
                }
                else
                {
                    rejectReason = "LAND requires airborne state";
                }
                break;
            case protocol::CommandId::EMERGENCY_STOP:
                if (state.droneState != protocol::DroneState::DISCONNECTED)
                {
                    transitionToLocked(state, protocol::DroneState::ERROR, now, "CMD_EMERGENCY_STOP");
                    success = true;
                    sendState = true;
                }
                else
                {
                    rejectReason = "EMERGENCY_STOP requires connection";
                }
                break;
            }
        }

        if (success)
        {
            sendAck(socket, protocol::MsgType::CMD_COMMAND, payload.commandId, protocol::AckResult::SUCCESS, "accepted");
            if (sendState)
            {
                sendStatePacket(socket);
            }
        }
        else
        {
            sendAck(socket, protocol::MsgType::CMD_COMMAND, payload.commandId, protocol::AckResult::REJECTED, rejectReason);
        }
    }

    void MockServer::handleMissionParameters(asio::ip::tcp::socket &socket, const protocol::PayloadMissionParams &payload)
    {
        if (payload.takeoffAltitudeM < 1.0f || payload.takeoffAltitudeM > 100.0f || payload.flightSpeedMS < 0.5f ||
            payload.flightSpeedMS > 20.0f)
        {
            sendAck(socket, protocol::MsgType::CMD_SET_PARAMS, 0, protocol::AckResult::INVALID_PARAM, "param out of range");
            return;
        }

        {
            std::scoped_lock lock(stateMutex);
            state.missionParameters = payload;
        }

        log("mission params updated");
        sendAck(socket, protocol::MsgType::CMD_SET_PARAMS, 0, protocol::AckResult::SUCCESS, "params stored");
    }

    void MockServer::handleMode(asio::ip::tcp::socket &socket, const protocol::PayloadSetMode &payload)
    {
        if (payload.mode != static_cast<std::uint8_t>(protocol::FlightMode::AUTOMATIC) &&
            payload.mode != static_cast<std::uint8_t>(protocol::FlightMode::SEMI_AUTOMATIC))
        {
            sendAck(socket, protocol::MsgType::CMD_SET_MODE, 0, protocol::AckResult::INVALID_PARAM, "unknown mode");
            return;
        }

        {
            std::scoped_lock lock(stateMutex);
            state.flightMode = static_cast<protocol::FlightMode>(payload.mode);
        }

        log(std::string("flight mode -> ") + protocol::flightModeToString(static_cast<protocol::FlightMode>(payload.mode)));
        sendAck(socket, protocol::MsgType::CMD_SET_MODE, 0, protocol::AckResult::SUCCESS, "mode stored");
        sendStatePacket(socket);
    }

    void MockServer::handleObstacles(asio::ip::tcp::socket &socket, const protocol::PayloadSimObstacles &payload)
    {
        bool shouldSendState = false;
        {
            std::scoped_lock lock(stateMutex);
            state.obstacles = payload;
            if (state.droneState == protocol::DroneState::EXECUTING_MISSION && state.obstacles.front)
            {
                transitionToLocked(state, protocol::DroneState::PAUSED, std::chrono::steady_clock::now(), "front obstacle");
                state.frontObstacleAutoPauseLatched = true;
                shouldSendState = true;
            }
        }

        log("simulation obstacles updated");
        sendAck(socket, protocol::MsgType::CMD_SIM_OBSTACLES, 0, protocol::AckResult::SUCCESS, "obstacles stored");
        if (shouldSendState)
        {
            sendStatePacket(socket);
        }
    }

    void MockServer::handleLidar(asio::ip::tcp::socket &socket, const protocol::PayloadSimLidar &payload)
    {
        {
            std::scoped_lock lock(stateMutex);
            state.lidarActive = payload.lidarActive;
        }

        log(std::string("lidar simulation -> ") + (payload.lidarActive ? "ON" : "OFF"));
        sendAck(socket, protocol::MsgType::CMD_SIM_LIDAR, 0, protocol::AckResult::SUCCESS, "lidar flag stored");
    }

    void MockServer::sendStatePacket(asio::ip::tcp::socket &socket)
    {
        protocol::PayloadTelemetryState telemetryState{};
        {
            std::scoped_lock lock(stateMutex);
            telemetryState = makeTelemetryStateLocked(state);
        }

        const auto packet = protocol::serializePacket(protocol::MsgType::TEL_STATE, telemetryState);
        std::error_code errorCode;
        asio::write(socket, asio::buffer(packet), errorCode);
        if (errorCode)
        {
            log("send state failed: " + errorCode.message());
        }
    }

    void MockServer::sendAck(asio::ip::tcp::socket &socket,
                            protocol::MsgType originalMsgType,
                            std::uint8_t originalCommandId,
                            protocol::AckResult result,
                            std::string_view message)
    {
        protocol::PayloadAck ack{};
        ack.originalMsgType = static_cast<std::uint8_t>(originalMsgType);
        ack.originalCommandId = originalCommandId;
        ack.result = static_cast<std::uint8_t>(result);

        const auto copyLength = std::min(message.size(), sizeof(ack.message) - 1);
        std::memcpy(ack.message, message.data(), copyLength);
        ack.message[copyLength] = '\0';

        const auto packet = protocol::serializePacket(protocol::MsgType::TEL_ACK, ack);
        std::error_code errorCode;
        asio::write(socket, asio::buffer(packet), errorCode);
        if (errorCode)
        {
            log("send ACK failed: " + errorCode.message());
        }
    }

    void MockServer::sendPositionUdp(const TelemetrySnapshot &snapshot, asio::ip::udp::socket &udpSocket)
    {
        const auto packet = protocol::serializePacket(protocol::MsgType::TEL_POSITION, snapshot.position);
        std::error_code errorCode;
        udpSocket.send_to(asio::buffer(packet), *snapshot.udpTargetEndpoint, 0, errorCode);
        if (errorCode)
        {
            log("UDP position send failed: " + errorCode.message());
        }
    }

    void MockServer::sendPointCloudUdp(const TelemetrySnapshot &snapshot, asio::ip::udp::socket &udpSocket)
    {
        const auto timestampMs = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt).count());
        const auto payload = buildPointCloudPayload(timestampMs);
        const auto packet = protocol::serializePacket(protocol::MsgType::TEL_POINT_CLOUD, std::span<const std::uint8_t>(payload.data(), payload.size()));

        std::error_code errorCode;
        udpSocket.send_to(asio::buffer(packet), *snapshot.udpTargetEndpoint, 0, errorCode);
        if (errorCode)
        {
            log("UDP point cloud send failed: " + errorCode.message());
        }
    }

    void MockServer::log(std::string_view message) const
    {
        std::cout << "[mock_server] " << message << std::endl;
    }

    MockServerConfig parseMockServerConfig(int argc, char **argv)
    {
        MockServerConfig config;

        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            const auto nextValue = [&](const char *flag) -> std::string {
                if (index + 1 >= argc)
                {
                    throw std::runtime_error(std::string("missing value for ") + flag);
                }
                return argv[++index];
            };

            if (argument == "--tcp-port")
            {
                config.tcpPort = static_cast<std::uint16_t>(std::stoi(nextValue("--tcp-port")));
            }
            else if (argument == "--udp-port")
            {
                config.udpPort = static_cast<std::uint16_t>(std::stoi(nextValue("--udp-port")));
            }
            else if (argument == "--runtime-sec")
            {
                config.runtimeLimit = std::chrono::seconds(std::stoi(nextValue("--runtime-sec")));
            }
        }

        return config;
    }

}