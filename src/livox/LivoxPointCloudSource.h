#pragma once

#include "state/SharedState.h"

#include <asio/ip/udp.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace gcs::livox
{

    class LivoxPointCloudSource
    {
    public:
        explicit LivoxPointCloudSource(SharedState &sharedState);
        ~LivoxPointCloudSource();

        LivoxPointCloudSource(const LivoxPointCloudSource &) = delete;
        LivoxPointCloudSource &operator=(const LivoxPointCloudSource &) = delete;

        void start();
        void stop();
        void restart();

    private:
        struct FrameKey
        {
            std::uint64_t timestampNs = 0;
            std::uint8_t frameCounter = 0;
            std::uint8_t dataType = 0;

            bool operator==(const FrameKey &other) const = default;
        };

        struct PartialFrame
        {
            FrameKey key;
            std::vector<protocol::PointCloudPoint> points;
            std::unordered_set<std::uint16_t> packetIds;
            std::uint32_t packetsReceived = 0;
            std::chrono::steady_clock::time_point startedAt{};
            std::chrono::steady_clock::time_point lastPacketAt{};
        };

        struct ParsedPacket
        {
            FrameKey key;
            std::uint16_t udpCount = 0;
            std::vector<protocol::PointCloudPoint> points;
        };

        void run(std::stop_token stopToken);
        void closeActiveSocket();
        bool tryParsePacket(std::span<const std::uint8_t> packetBytes, ParsedPacket &packet) const;
        void handlePacket(ParsedPacket packet);
        void maybeFlushStaleFrame(std::chrono::steady_clock::time_point now);
        void publishCurrentFrame();
        void dropCurrentFrame(const std::string &reason);
        static std::uint64_t parseTimestampNs(const std::uint8_t timestampBytes[8]);

        SharedState &sharedState;
        std::mutex socketMutex;
        asio::ip::udp::socket *activeSocket = nullptr;
        std::jthread workerThread;
        std::optional<PartialFrame> currentFrame;
    };

} // namespace gcs::livox