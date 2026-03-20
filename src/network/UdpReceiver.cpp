#include "network/UdpReceiver.h"

#include <asio/io_context.hpp>

#include <array>
#include <chrono>
#include <thread>

namespace gcs::network
{

    UdpReceiver::UdpReceiver(PacketHandler packetHandlerValue, ErrorHandler errorHandlerValue)
        : packetHandler(std::move(packetHandlerValue)),
        errorHandler(std::move(errorHandlerValue))
    {
    }

    UdpReceiver::~UdpReceiver()
    {
        stop();
    }

    void UdpReceiver::start(std::uint16_t port)
    {
        stop();
        {
            std::lock_guard lock(settingsMutex);
            listenPort = port;
        }
        workerThread = std::jthread([this](std::stop_token stopToken) { run(stopToken); });
    }

    void UdpReceiver::stop()
    {
        closeActiveSocket();
        if (workerThread.joinable())
        {
            workerThread.request_stop();
            workerThread.join();
        }
    }

    void UdpReceiver::run(std::stop_token stopToken)
    {
        std::uint16_t currentPort = 5761;
        {
            std::lock_guard lock(settingsMutex);
            currentPort = listenPort;
        }

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
            emitError(errorCode.message());
            return;
        }

        socket.set_option(asio::socket_base::reuse_address(true), errorCode);
        socket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), currentPort), errorCode);
        if (errorCode)
        {
            emitError(errorCode.message());
            std::lock_guard lock(socketMutex);
            activeSocket = nullptr;
            return;
        }

        socket.non_blocking(true, errorCode);
        if (errorCode)
        {
            emitError(errorCode.message());
            std::lock_guard lock(socketMutex);
            activeSocket = nullptr;
            return;
        }

        std::array<std::uint8_t, 65535> receiveBuffer{};
        asio::ip::udp::endpoint remoteEndpoint;

        while (!stopToken.stop_requested())
        {
            const std::size_t bytesReceived = socket.receive_from(asio::buffer(receiveBuffer), remoteEndpoint, 0, errorCode);
            if (!errorCode)
            {
                packetHandler(std::vector<std::uint8_t>(receiveBuffer.begin(), receiveBuffer.begin() + bytesReceived));
                continue;
            }

            if (errorCode == asio::error::would_block || errorCode == asio::error::try_again)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            if (!stopToken.stop_requested())
            {
                emitError(errorCode.message());
            }
            break;
        }

        std::error_code ignoredError;
        socket.close(ignoredError);
        {
            std::lock_guard lock(socketMutex);
            activeSocket = nullptr;
        }
    }

    void UdpReceiver::closeActiveSocket()
    {
        std::lock_guard lock(socketMutex);
        if (activeSocket != nullptr)
        {
            std::error_code ignoredError;
            activeSocket->cancel(ignoredError);
            activeSocket->close(ignoredError);
        }
    }

    void UdpReceiver::emitError(std::string message) const
    {
        if (errorHandler)
        {
            errorHandler(std::move(message));
        }
    }

}