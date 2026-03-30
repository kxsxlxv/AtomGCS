#include "livox/LivoxPointCloudSource.h"

#include <livox_lidar_def.h>

#include <asio/io_context.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <thread>

namespace gcs::livox
{

    namespace
    {
        constexpr auto frameFlushTimeout = std::chrono::milliseconds(150);
        constexpr auto receivePollInterval = std::chrono::milliseconds(10);
        constexpr int livoxReceiveBufferBytes = 4 * 1024 * 1024;

        std::uint32_t timestampToDisplayMs(std::uint64_t timestampNs)
        {
            return static_cast<std::uint32_t>(timestampNs / 1'000'000ULL);
        }
    }

    LivoxPointCloudSource::LivoxPointCloudSource(SharedState &sharedStateValue)
        : sharedState(sharedStateValue)
    {
    }

    LivoxPointCloudSource::~LivoxPointCloudSource()
    {
        stop();
    }

    void LivoxPointCloudSource::start()
    {
        stop();

        if (!sharedState.getLidarSettings().enabled)
        {
            return;
        }

        workerThread = std::jthread([this](std::stop_token stopToken) { run(stopToken); });
    }

    void LivoxPointCloudSource::stop()
    {
        closeActiveSocket();
        if (workerThread.joinable())
        {
            workerThread.request_stop();
            workerThread.join();
        }
        currentFrame.reset();
    }

    void LivoxPointCloudSource::restart()
    {
        stop();
        start();
    }

    void LivoxPointCloudSource::run(std::stop_token stopToken)
    {
        const auto settings = sharedState.getLidarSettings();

        asio::io_context ioContext;
        asio::ip::udp::socket socket(ioContext);
        {
            std::lock_guard lock(socketMutex);
            activeSocket = &socket;
        }

        std::error_code errorCode;
        socket.open(asio::ip::udp::v4(), errorCode);
        if (errorCode)
        {
            sharedState.appendLog(SharedState::LogDirection::local,
                                  SharedState::LogCategory::error,
                                  "LIVOX",
                                  "UDP open failed: " + errorCode.message());
            return;
        }

        socket.set_option(asio::socket_base::reuse_address(true), errorCode);
        socket.set_option(asio::socket_base::receive_buffer_size(livoxReceiveBufferBytes), errorCode);
        socket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), settings.pointDataPort), errorCode);
        if (errorCode)
        {
            sharedState.appendLog(SharedState::LogDirection::local,
                                  SharedState::LogCategory::error,
                                  "LIVOX",
                                  "UDP bind failed: " + errorCode.message());
            std::lock_guard lock(socketMutex);
            activeSocket = nullptr;
            return;
        }

        socket.non_blocking(true, errorCode);
        if (errorCode)
        {
            sharedState.appendLog(SharedState::LogDirection::local,
                                  SharedState::LogCategory::error,
                                  "LIVOX",
                                  "UDP non-blocking failed: " + errorCode.message());
            std::lock_guard lock(socketMutex);
            activeSocket = nullptr;
            return;
        }

        sharedState.appendLog(SharedState::LogDirection::local,
                              SharedState::LogCategory::telemetry,
                              "LIVOX",
                              "Listening raw Livox UDP on port " + std::to_string(settings.pointDataPort));

        std::array<std::uint8_t, 65535> receiveBuffer{};
        asio::ip::udp::endpoint remoteEndpoint;

        while (!stopToken.stop_requested())
        {
            const std::size_t bytesReceived = socket.receive_from(asio::buffer(receiveBuffer), remoteEndpoint, 0, errorCode);
            if (!errorCode)
            {
                ParsedPacket packet;
                if (tryParsePacket(std::span<const std::uint8_t>(receiveBuffer.data(), bytesReceived), packet))
                {
                    sharedState.noteLidarPacketReceived();
                    handlePacket(std::move(packet));
                }
                maybeFlushStaleFrame(std::chrono::steady_clock::now());
                continue;
            }

            if (errorCode == asio::error::would_block || errorCode == asio::error::try_again)
            {
                maybeFlushStaleFrame(std::chrono::steady_clock::now());
                std::this_thread::sleep_for(receivePollInterval);
                continue;
            }

            if (!stopToken.stop_requested())
            {
                sharedState.appendLog(SharedState::LogDirection::local,
                                      SharedState::LogCategory::error,
                                      "LIVOX",
                                      "UDP receive failed: " + errorCode.message());
            }
            break;
        }

        maybeFlushStaleFrame(std::chrono::steady_clock::now());

        std::error_code ignoredError;
        socket.close(ignoredError);
        {
            std::lock_guard lock(socketMutex);
            activeSocket = nullptr;
        }
    }

    void LivoxPointCloudSource::closeActiveSocket()
    {
        std::lock_guard lock(socketMutex);
        if (activeSocket != nullptr)
        {
            std::error_code ignoredError;
            activeSocket->cancel(ignoredError);
            activeSocket->close(ignoredError);
        }
    }

    bool LivoxPointCloudSource::tryParsePacket(std::span<const std::uint8_t> packetBytes, ParsedPacket &packet) const
    {
        constexpr std::size_t headerSize = offsetof(LivoxLidarEthernetPacket, data);
        if (packetBytes.size() < headerSize)
        {
            return false;
        }

        const auto *header = reinterpret_cast<const LivoxLidarEthernetPacket *>(packetBytes.data());
        if (header->data_type != kLivoxLidarCartesianCoordinateHighData)
        {
            return false;
        }

        const std::size_t expectedSize =
            headerSize + static_cast<std::size_t>(header->dot_num) * sizeof(LivoxLidarCartesianHighRawPoint);
        if (packetBytes.size() < expectedSize)
        {
            return false;
        }

        packet.key.timestampNs = parseTimestampNs(header->timestamp);
        packet.key.frameCounter = header->frame_cnt;
        packet.key.dataType = header->data_type;
        packet.udpCount = header->udp_cnt;
        packet.points.resize(header->dot_num);

        const auto *rawPoints = reinterpret_cast<const LivoxLidarCartesianHighRawPoint *>(header->data);
        for (std::size_t index = 0; index < packet.points.size(); ++index)
        {
            packet.points[index].x = static_cast<float>(rawPoints[index].x) * 0.001f;
            packet.points[index].y = static_cast<float>(rawPoints[index].y) * 0.001f;
            packet.points[index].z = static_cast<float>(rawPoints[index].z) * 0.001f;
            packet.points[index].intensity = rawPoints[index].reflectivity;
        }

        return true;
    }

    void LivoxPointCloudSource::handlePacket(ParsedPacket packet)
    {
        const auto now = std::chrono::steady_clock::now();

        if (!currentFrame.has_value())
        {
            currentFrame = PartialFrame{.key = packet.key, .startedAt = now, .lastPacketAt = now};
        }
        else if (currentFrame->key != packet.key)
        {
            publishCurrentFrame();
            currentFrame = PartialFrame{.key = packet.key, .startedAt = now, .lastPacketAt = now};
        }

        if (currentFrame->packetIds.insert(packet.udpCount).second)
        {
            currentFrame->points.insert(currentFrame->points.end(), packet.points.begin(), packet.points.end());
            currentFrame->packetsReceived += 1;
        }
        currentFrame->lastPacketAt = now;
    }

    void LivoxPointCloudSource::maybeFlushStaleFrame(std::chrono::steady_clock::time_point now)
    {
        if (!currentFrame.has_value())
        {
            return;
        }

        if (now - currentFrame->lastPacketAt >= frameFlushTimeout)
        {
            publishCurrentFrame();
        }
    }

    void LivoxPointCloudSource::publishCurrentFrame()
    {
        if (!currentFrame.has_value())
        {
            return;
        }

        auto frame = std::move(*currentFrame);
        currentFrame.reset();

        if (frame.points.empty())
        {
            sharedState.incrementDroppedLidarFrame();
            return;
        }

        const auto pointCount = static_cast<std::uint32_t>(frame.points.size());
        sharedState.updatePointCloud(timestampToDisplayMs(frame.key.timestampNs), std::move(frame.points));
        sharedState.updateLidarFrame(frame.key.frameCounter, pointCount);
        sharedState.appendLog(SharedState::LogDirection::inbound,
                              SharedState::LogCategory::telemetry,
                              "LIVOX_FRAME",
                              "Livox frame " + std::to_string(frame.key.frameCounter) + ": " +
                                  std::to_string(pointCount) + " points, " +
                                  std::to_string(frame.packetsReceived) + " packets");
    }

    void LivoxPointCloudSource::dropCurrentFrame(const std::string &reason)
    {
        if (!currentFrame.has_value())
        {
            return;
        }

        currentFrame.reset();
        sharedState.incrementDroppedLidarFrame();
        sharedState.appendLog(SharedState::LogDirection::local,
                              SharedState::LogCategory::error,
                              "LIVOX",
                              reason);
    }

    std::uint64_t LivoxPointCloudSource::parseTimestampNs(const std::uint8_t timestampBytes[8])
    {
        std::uint64_t value = 0;
        std::memcpy(&value, timestampBytes, sizeof(value));
        return value;
    }

} // namespace gcs::livox