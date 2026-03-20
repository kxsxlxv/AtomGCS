#pragma once

#include "state/SharedState.h"

#include <asio/ip/tcp.hpp>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <thread>
#include <mutex>
#include <string>
#include <vector>

namespace gcs::network
{

    class TcpClient
    {
    public:
        using PacketHandler = std::function<void(std::vector<std::uint8_t>)>;
        using StatusHandler = std::function<void(SharedState::ConnectionStatus, std::string)>;

        TcpClient(PacketHandler packetHandler, StatusHandler statusHandler);
        ~TcpClient();

        TcpClient(const TcpClient &) = delete;
        TcpClient &operator=(const TcpClient &) = delete;

        void start(const SharedState::ConnectionSettings &settings);
        void stop();
        bool send(std::vector<std::uint8_t> packet);

    private:
        void run(std::stop_token stopToken);
        bool connectSocket(std::stop_token stopToken, asio::ip::tcp::socket &socket);
        bool flushOutboundQueue(asio::ip::tcp::socket &socket);
        void closeActiveSocket();
        void emitStatus(SharedState::ConnectionStatus status, std::string message) const;

        PacketHandler packetHandler;
        StatusHandler statusHandler;

        mutable std::mutex settingsMutex;
        SharedState::ConnectionSettings settings;

        std::mutex queueMutex;
        std::condition_variable_any queueCondition;
        std::deque<std::vector<std::uint8_t>> outboundQueue;

        std::mutex socketMutex;
        asio::ip::tcp::socket *activeSocket = nullptr;

        std::jthread workerThread;
        bool manualDisconnect = true;
    };

}