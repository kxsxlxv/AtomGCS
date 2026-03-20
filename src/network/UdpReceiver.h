#pragma once

#include <asio/ip/udp.hpp>

#include <cstdint>
#include <functional>
#include <thread>
#include <mutex>
#include <string>
#include <vector>

namespace gcs::network
{

    class UdpReceiver
    {
    public:
        using PacketHandler = std::function<void(std::vector<std::uint8_t>)>;
        using ErrorHandler = std::function<void(std::string)>;

        UdpReceiver(PacketHandler packetHandler, ErrorHandler errorHandler);
        ~UdpReceiver();

        UdpReceiver(const UdpReceiver &) = delete;
        UdpReceiver &operator=(const UdpReceiver &) = delete;

        void start(std::uint16_t port);
        void stop();

    private:
        void run(std::stop_token stopToken);
        void closeActiveSocket();
        void emitError(std::string message) const;

        PacketHandler packetHandler;
        ErrorHandler errorHandler;

        std::mutex settingsMutex;
        std::uint16_t listenPort = 5761;

        std::mutex socketMutex;
        asio::ip::udp::socket *activeSocket = nullptr;

        std::jthread workerThread;
    };

}