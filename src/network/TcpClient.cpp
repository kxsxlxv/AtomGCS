#include "network/TcpClient.h"

#include "shared/protocol/protocol_utils.h"

#include <asio/connect.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/write.hpp>

#include <array>
#include <chrono>
#include <thread>

namespace gcs::network
{

    TcpClient::TcpClient(PacketHandler packetHandlerValue, StatusHandler statusHandlerValue)
        : packetHandler(std::move(packetHandlerValue)),
        statusHandler(std::move(statusHandlerValue))
    {
    }

    TcpClient::~TcpClient()
    {
        stop();
    }

    void TcpClient::start(const SharedState::ConnectionSettings &newSettings)
    {
        stop();

        {
            std::lock_guard lock(settingsMutex);
            settings = newSettings;
            manualDisconnect = false;
        }

        workerThread = std::jthread([this](std::stop_token stopToken) { run(stopToken); });
    }

    void TcpClient::stop()
    {
        {
            std::lock_guard lock(settingsMutex);
            manualDisconnect = true;
        }

        closeActiveSocket();
        queueCondition.notify_one();

        if (workerThread.joinable())
        {
            workerThread.request_stop();
            workerThread.join();
        }

        {
            std::lock_guard lock(queueMutex);
            outboundQueue.clear();
        }
    }

    bool TcpClient::send(std::vector<std::uint8_t> packet)
    {
        {
            std::lock_guard lock(queueMutex);
            outboundQueue.push_back(std::move(packet));
        }
        queueCondition.notify_one();
        return workerThread.joinable();
    }

    void TcpClient::run(std::stop_token stopToken)
    {
        while (!stopToken.stop_requested())
        {
            emitStatus(SharedState::ConnectionStatus::connecting, "Connecting to control module");

            asio::io_context ioContext;
            asio::ip::tcp::socket socket(ioContext);
            {
                std::lock_guard lock(socketMutex);
                activeSocket = &socket;
            }

            if (!connectSocket(stopToken, socket))
            {
                {
                    std::lock_guard lock(socketMutex);
                    activeSocket = nullptr;
                }

                if (stopToken.stop_requested())
                {
                    break;
                }

                const bool shouldReconnect = [&]() {
                    std::lock_guard lock(settingsMutex);
                    return !manualDisconnect;
                }();
                if (!shouldReconnect)
                {
                    break;
                }

                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            emitStatus(SharedState::ConnectionStatus::connected, "TCP connected");

            std::error_code errorCode;
            socket.non_blocking(true, errorCode);
            if (errorCode)
            {
                emitStatus(SharedState::ConnectionStatus::disconnected, errorCode.message());
                break;
            }

            protocol::PacketStreamParser parser;
            std::array<std::uint8_t, 4096> readBuffer{};

            while (!stopToken.stop_requested())
            {
                if (!flushOutboundQueue(socket))
                {
                    break;
                }

                const std::size_t bytesRead = socket.read_some(asio::buffer(readBuffer), errorCode);
                if (!errorCode)
                {
                    parser.append(std::span<const std::uint8_t>(readBuffer.data(), bytesRead));
                    for (auto &packetBytes : parser.extractPackets())
                    {
                        packetHandler(std::move(packetBytes));
                    }
                    continue;
                }

                if (errorCode == asio::error::would_block || errorCode == asio::error::try_again)
                {
                    std::unique_lock queueLock(queueMutex);
                    queueCondition.wait_for(queueLock, std::chrono::milliseconds(20));
                    continue;
                }

                emitStatus(SharedState::ConnectionStatus::disconnected, errorCode.message());
                break;
            }

            std::error_code ignoredError;
            socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignoredError);
            socket.close(ignoredError);
            {
                std::lock_guard lock(socketMutex);
                activeSocket = nullptr;
            }

            const bool shouldReconnect = [&]() {
                std::lock_guard lock(settingsMutex);
                return !manualDisconnect;
            }();

            if (stopToken.stop_requested() || !shouldReconnect)
            {
                break;
            }

            emitStatus(SharedState::ConnectionStatus::disconnected, "TCP disconnected, reconnecting");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        emitStatus(SharedState::ConnectionStatus::disconnected, "TCP stopped");
    }

    bool TcpClient::connectSocket(std::stop_token stopToken, asio::ip::tcp::socket &socket)
    {
        SharedState::ConnectionSettings currentSettings;
        {
            std::lock_guard lock(settingsMutex);
            currentSettings = settings;
        }

        asio::io_context &ioContext = static_cast<asio::io_context &>(socket.get_executor().context());
        asio::ip::tcp::resolver resolver(ioContext);

        std::error_code resolveError;
        const auto endpoints = resolver.resolve(currentSettings.ipAddress, std::to_string(currentSettings.tcpPort), resolveError);
        if (resolveError)
        {
            emitStatus(SharedState::ConnectionStatus::disconnected, resolveError.message());
            return false;
        }

        std::error_code connectError = asio::error::would_block;
        bool connectFinished = false;

        asio::steady_timer timeoutTimer(ioContext);
        timeoutTimer.expires_after(std::chrono::seconds(5));
        timeoutTimer.async_wait([&](const std::error_code &errorCode) {
            if (!errorCode && !connectFinished)
            {
                connectError = asio::error::timed_out;
                socket.cancel();
                socket.close();
            }
        });

        asio::async_connect(socket, endpoints, [&](const std::error_code &errorCode, const asio::ip::tcp::endpoint &) {
            connectFinished = true;
            connectError = errorCode;
            timeoutTimer.cancel();
        });

        ioContext.restart();
        while (!connectFinished && !stopToken.stop_requested())
        {
            ioContext.run_one();
        }

        timeoutTimer.cancel();

        if (stopToken.stop_requested())
        {
            return false;
        }

        if (connectError)
        {
            emitStatus(SharedState::ConnectionStatus::disconnected, connectError.message());
            return false;
        }

        return true;
    }

    bool TcpClient::flushOutboundQueue(asio::ip::tcp::socket &socket)
    {
        while (true)
        {
            std::vector<std::uint8_t> packet;
            {
                std::lock_guard lock(queueMutex);
                if (outboundQueue.empty())
                {
                    return true;
                }
                packet = std::move(outboundQueue.front());
                outboundQueue.pop_front();
            }

            std::error_code errorCode;
            asio::write(socket, asio::buffer(packet), errorCode);
            if (errorCode)
            {
                emitStatus(SharedState::ConnectionStatus::disconnected, errorCode.message());
                return false;
            }
        }
    }

    void TcpClient::closeActiveSocket()
    {
        std::lock_guard lock(socketMutex);
        if (activeSocket != nullptr)
        {
            std::error_code ignoredError;
            activeSocket->cancel(ignoredError);
            activeSocket->close(ignoredError);
        }
    }

    void TcpClient::emitStatus(SharedState::ConnectionStatus status, std::string message) const
    {
        if (statusHandler)
        {
            statusHandler(status, std::move(message));
        }
    }

}