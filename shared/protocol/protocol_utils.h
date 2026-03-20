#pragma once

#include "shared/protocol/protocol.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#ifdef ERROR
#undef ERROR
#endif

namespace gcs::protocol
{

constexpr std::size_t packetOverheadSize = sizeof(PacketHeader) + sizeof(std::uint8_t);
constexpr std::size_t minimumPacketSize = packetOverheadSize;
constexpr std::size_t maxPointCloudPointsPerPacket =
    (static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) - sizeof(PayloadPointCloudHeader)) /
    sizeof(PointCloudPoint);

inline std::uint8_t crc8Atm(std::span<const std::uint8_t> bytes)
{
    std::uint8_t crc = 0x00;
    for (const std::uint8_t byte : bytes)
    {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
        {
            const bool highBitSet = (crc & 0x80u) != 0;
            crc <<= 1u;
            if (highBitSet)
            {
                crc ^= 0x07u;
            }
        }
    }
    return crc;
}

inline std::uint16_t readUint16Le(const std::uint8_t *bytes)
{
    return static_cast<std::uint16_t>(bytes[0]) | (static_cast<std::uint16_t>(bytes[1]) << 8u);
}

inline void writeUint16Le(std::uint8_t *bytes, std::uint16_t value)
{
    bytes[0] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

inline std::uint32_t commandBit(CommandId commandId)
{
    const auto raw = static_cast<std::uint8_t>(commandId);
    if (raw == 0 || raw > 32)
    {
        return 0;
    }
    return 1u << (raw - 1u);
}

inline bool isCommandAvailable(std::uint32_t availableCommands, CommandId commandId)
{
    return (availableCommands & commandBit(commandId)) != 0;
}

inline const char *msgTypeToString(MsgType msgType)
{
    switch (msgType)
    {
    case MsgType::CMD_COMMAND:
        return "CMD_COMMAND";
    case MsgType::CMD_SET_PARAMS:
        return "CMD_SET_PARAMS";
    case MsgType::CMD_SET_MODE:
        return "CMD_SET_MODE";
    case MsgType::CMD_SIM_OBSTACLES:
        return "CMD_SIM_OBSTACLES";
    case MsgType::CMD_SIM_LIDAR:
        return "CMD_SIM_LIDAR";
    case MsgType::TEL_STATE:
        return "TEL_STATE";
    case MsgType::TEL_POSITION:
        return "TEL_POSITION";
    case MsgType::TEL_POINT_CLOUD:
        return "TEL_POINT_CLOUD";
    case MsgType::TEL_ACK:
        return "TEL_ACK";
    }
    return "UNKNOWN";
}

inline const char *commandIdToString(CommandId commandId)
{
    switch (commandId)
    {
    case CommandId::PREPARE:
        return "PREPARE";
    case CommandId::TAKEOFF:
        return "TAKEOFF";
    case CommandId::START_MISSION:
        return "START_MISSION";
    case CommandId::PAUSE_RESUME:
        return "PAUSE_RESUME";
    case CommandId::RETURN_HOME:
        return "RETURN_HOME";
    case CommandId::LAND:
        return "LAND";
    case CommandId::EMERGENCY_STOP:
        return "EMERGENCY_STOP";
    }
    return "UNKNOWN";
}

inline const char *flightModeToString(FlightMode mode)
{
    switch (mode)
    {
    case FlightMode::AUTOMATIC:
        return "Automatic";
    case FlightMode::SEMI_AUTOMATIC:
        return "Semi-Automatic";
    }
    return "Unknown";
}

inline const char *droneStateToString(DroneState state)
{
    switch (state)
    {
    case DroneState::DISCONNECTED:
        return "Disconnected";
    case DroneState::CONNECTED:
        return "Connected";
    case DroneState::IDLE:
        return "Idle";
    case DroneState::PREPARING:
        return "Preparing";
    case DroneState::READY:
        return "Ready";
    case DroneState::ARMING:
        return "Arming";
    case DroneState::TAKING_OFF:
        return "Taking off";
    case DroneState::IN_FLIGHT:
        return "In flight";
    case DroneState::EXECUTING_MISSION:
        return "Executing mission";
    case DroneState::PAUSED:
        return "Paused";
    case DroneState::RETURNING_HOME:
        return "Returning home";
    case DroneState::LANDING:
        return "Landing";
    case DroneState::LANDED:
        return "Landed";
    case DroneState::ERROR:
        return "Error";
    case DroneState::EMERGENCY_LANDING:
        return "Emergency landing";
    }
    return "Unknown";
}

inline const char *ackResultToString(AckResult result)
{
    switch (result)
    {
    case AckResult::SUCCESS:
        return "SUCCESS";
    case AckResult::REJECTED:
        return "REJECTED";
    case AckResult::INVALID_PARAM:
        return "INVALID_PARAM";
    case AckResult::ERROR:
        return "ERROR";
    }
    return "UNKNOWN";
}

template <typename Payload>
inline bool parsePayload(std::span<const std::uint8_t> bytes, Payload &payload)
{
    if (bytes.size() != sizeof(Payload))
    {
        return false;
    }

    std::memcpy(&payload, bytes.data(), sizeof(Payload));
    return true;
}

inline std::vector<std::uint8_t> serializePacket(MsgType msgType, std::span<const std::uint8_t> payloadBytes)
{
    if (payloadBytes.size() > std::numeric_limits<std::uint16_t>::max())
    {
        return {};
    }

    std::vector<std::uint8_t> packet(packetOverheadSize + payloadBytes.size());
    packet[0] = packetMagic[0];
    packet[1] = packetMagic[1];
    packet[2] = protocolVersion;
    packet[3] = static_cast<std::uint8_t>(msgType);
    writeUint16Le(packet.data() + 4, static_cast<std::uint16_t>(payloadBytes.size()));

    if (!payloadBytes.empty())
    {
        std::memcpy(packet.data() + sizeof(PacketHeader), payloadBytes.data(), payloadBytes.size());
    }

    packet[packet.size() - 1] = crc8Atm(std::span<const std::uint8_t>(packet.data(), packet.size() - 1));
    return packet;
}

template <typename Payload>
inline std::vector<std::uint8_t> serializePacket(MsgType msgType, const Payload &payload)
{
    return serializePacket(
        msgType,
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(&payload), sizeof(Payload)));
}

struct PacketView
{
    MsgType msgType{};
    std::span<const std::uint8_t> payload;
};

enum class PacketParseError
{
    None,
    TooShort,
    InvalidMagic,
    InvalidVersion,
    LengthMismatch,
    CrcMismatch,
};

inline std::optional<PacketView> tryParsePacket(std::span<const std::uint8_t> packetBytes,
                                                PacketParseError *error = nullptr)
{
    if (packetBytes.size() < minimumPacketSize)
    {
        if (error != nullptr)
        {
            *error = PacketParseError::TooShort;
        }
        return std::nullopt;
    }

    if (packetBytes[0] != packetMagic[0] || packetBytes[1] != packetMagic[1])
    {
        if (error != nullptr)
        {
            *error = PacketParseError::InvalidMagic;
        }
        return std::nullopt;
    }

    if (packetBytes[2] != protocolVersion)
    {
        if (error != nullptr)
        {
            *error = PacketParseError::InvalidVersion;
        }
        return std::nullopt;
    }

    const std::uint16_t payloadLength = readUint16Le(packetBytes.data() + 4);
    const std::size_t packetSize = packetOverheadSize + payloadLength;
    if (packetBytes.size() != packetSize)
    {
        if (error != nullptr)
        {
            *error = PacketParseError::LengthMismatch;
        }
        return std::nullopt;
    }

    const std::uint8_t expectedCrc = crc8Atm(packetBytes.first(packetBytes.size() - 1));
    if (expectedCrc != packetBytes[packetBytes.size() - 1])
    {
        if (error != nullptr)
        {
            *error = PacketParseError::CrcMismatch;
        }
        return std::nullopt;
    }

    if (error != nullptr)
    {
        *error = PacketParseError::None;
    }

    return PacketView{
        static_cast<MsgType>(packetBytes[3]),
        packetBytes.subspan(sizeof(PacketHeader), payloadLength),
    };
}

class PacketStreamParser
{
  public:
    void append(std::span<const std::uint8_t> bytes)
    {
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    }

    std::vector<std::vector<std::uint8_t>> extractPackets()
    {
        std::vector<std::vector<std::uint8_t>> packets;

        while (buffer.size() >= minimumPacketSize)
        {
            const auto magicPosition = std::search(buffer.begin(), buffer.end(), packetMagic.begin(), packetMagic.end());
            if (magicPosition == buffer.end())
            {
                buffer.clear();
                break;
            }

            if (magicPosition != buffer.begin())
            {
                buffer.erase(buffer.begin(), magicPosition);
            }

            if (buffer.size() < minimumPacketSize)
            {
                break;
            }

            const std::uint16_t payloadLength = readUint16Le(buffer.data() + 4);
            const std::size_t packetSize = packetOverheadSize + payloadLength;
            if (buffer.size() < packetSize)
            {
                break;
            }

            const std::span<const std::uint8_t> candidate(buffer.data(), packetSize);
            if (tryParsePacket(candidate).has_value())
            {
                packets.emplace_back(candidate.begin(), candidate.end());
                buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(packetSize));
            }
            else
            {
                buffer.erase(buffer.begin());
            }
        }

        return packets;
    }

  private:
    std::vector<std::uint8_t> buffer;
};

inline std::string ackMessageToString(const PayloadAck &ack)
{
    const auto terminator = std::find(std::begin(ack.message), std::end(ack.message), '\0');
    return std::string(std::begin(ack.message), terminator);
}

}