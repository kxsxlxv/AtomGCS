#pragma once

#include "shared/protocol/protocol.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gcs::protocol
{

    constexpr std::size_t packetOverheadSize = sizeof(PacketHeader) + sizeof(std::uint8_t);
    constexpr std::size_t minimumPacketSize = packetOverheadSize;
    constexpr std::size_t maxMissionPointsPerPacket =
        (static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) - sizeof(PayloadMissionParamsHeader)) /
        sizeof(MissionPointNed);
    constexpr std::size_t maxPointCloudPointsPerPacket =
        (static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) - sizeof(PayloadPointCloudPacketHeader)) /
        sizeof(PointCloudPoint);

    /*
    Расчёт контрольной суммы CRC-8/ITU (полином 0x07)
    
    Начальное значение CRC = 0x00

    Для каждого байта данных:
        CRC = CRC XOR байт
        Повторить 8 раз:
            Если старший бит CRC установлен:
                CRC = (CRC сдвиг влево на 1) XOR 0x07
            Иначе:
                CRC = CRC сдвиг влево на 1
    */
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
        case MsgType::CMD_SET_MISSION:
            return "CMD_SET_MISSION";
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
            return "ЧЕК";
        case CommandId::TAKEOFF:
            return "ВЗЛЁТ";
        case CommandId::START_MISSION:
            return "МИССИЯ";
        case CommandId::PAUSE_RESUME:
            return "ПАУЗА_ПРОДОЛЖИТЬ";
        case CommandId::RETURN_HOME:
            return "ВОЗВРАТ";
        case CommandId::LAND:
            return "ПОСАДКА";
        case CommandId::EMERGENCY_STOP:
            return "Аварийный стоп";
        }
        return "ХЗ";
    }

    inline const char *flightModeToString(FlightMode mode)
    {
        switch (mode)
        {
        case FlightMode::AUTOMATIC:
            return "АВТО";
        case FlightMode::SEMI_AUTOMATIC:
            return "ПОЛУ-АВТО";
        }
        return "ХЗ";
    }

    inline const char *droneStateToString(DroneState state)
    {
        switch (state)
        {
        case DroneState::DISCONNECTED: return "Отключён";
        case DroneState::CONNECTED: return "Подключён";
        case DroneState::IDLE: return "Ожидание";
        case DroneState::PREPARING: return "Подготовка";
        case DroneState::READY: return "Готов";
        case DroneState::ARMING: return "Активация моторов";
        case DroneState::TAKING_OFF: return "Взлёт";
        case DroneState::IN_FLIGHT: return "В полёте";
        case DroneState::EXECUTING_MISSION: return "Выполнение миссии";
        case DroneState::PAUSED: return "Пауза";
        case DroneState::RETURNING_HOME: return "Возврат домой";
        case DroneState::LANDING: return "Посадка";
        case DroneState::LANDED: return "Приземлился";
        case DroneState::INTERNAL_ERROR: return "Ошибка";
        case DroneState::EMERGENCY_LANDING: return "Аварийная посадка";
        }
        return "Неизвестно";
    }

    inline const char *ackResultToString(AckResult result)
    {
        switch (result)
        {
        case AckResult::SUCCESS:
            return "Успешно";
        case AckResult::REJECTED:
            return "Отклонено";
        case AckResult::INVALID_PARAM:
            return "Неверный параметр";
        case AckResult::INTERNAL_ERROR:
            return "Ошибка";
        }
        return "Неизвестно";
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

    inline std::vector<std::uint8_t> serializeMissionPayload(const PayloadMissionParamsHeader &header, std::span<const MissionPointNed> points)
    {
        if (points.size() != header.numPoints || points.size() > maxMissionPointsPerPacket)
        {
            return {};
        }

        std::vector<std::uint8_t> payload(sizeof(PayloadMissionParamsHeader) + points.size() * sizeof(MissionPointNed));
        std::memcpy(payload.data(), &header, sizeof(PayloadMissionParamsHeader));
        if (!points.empty())
        {
            std::memcpy(payload.data() + sizeof(PayloadMissionParamsHeader), points.data(), points.size() * sizeof(MissionPointNed));
        }
        return payload;
    }

    inline bool parseMissionPayload(std::span<const std::uint8_t> payloadBytes,
                                    PayloadMissionParamsHeader &header,
                                    std::vector<MissionPointNed> &points)
    {
        if (payloadBytes.size() < sizeof(PayloadMissionParamsHeader))
        {
            return false;
        }

        std::memcpy(&header, payloadBytes.data(), sizeof(PayloadMissionParamsHeader));
        const std::size_t expectedSize = sizeof(PayloadMissionParamsHeader) +
                                        static_cast<std::size_t>(header.numPoints) * sizeof(MissionPointNed);
        if (payloadBytes.size() != expectedSize || header.numPoints > maxMissionPointsPerPacket)
        {
            return false;
        }

        points.resize(header.numPoints);
        if (!points.empty())
        {
            std::memcpy(points.data(),
                        payloadBytes.data() + sizeof(PayloadMissionParamsHeader),
                        points.size() * sizeof(MissionPointNed));
        }
        return true;
    }

    inline std::vector<std::uint8_t> serializePointCloudPayload(const PayloadPointCloudPacketHeader &header, std::span<const PointCloudPoint> points)
    {
        if (points.size() != header.pointsInPacket || points.size() > maxPointCloudPointsPerPacket)
        {
            return {};
        }

        std::vector<std::uint8_t> payload(sizeof(PayloadPointCloudPacketHeader) + points.size() * sizeof(PointCloudPoint));
        std::memcpy(payload.data(), &header, sizeof(PayloadPointCloudPacketHeader));
        if (!points.empty())
        {
            std::memcpy(payload.data() + sizeof(PayloadPointCloudPacketHeader), points.data(), points.size() * sizeof(PointCloudPoint));
        }
        return payload;
    }

    inline bool parsePointCloudPayload(std::span<const std::uint8_t> payloadBytes,
                                    PayloadPointCloudPacketHeader &header,
                                    std::vector<PointCloudPoint> &points)
    {
        if (payloadBytes.size() < sizeof(PayloadPointCloudPacketHeader))
        {
            return false;
        }

        std::memcpy(&header, payloadBytes.data(), sizeof(PayloadPointCloudPacketHeader));
        const std::size_t expectedSize = sizeof(PayloadPointCloudPacketHeader) +
                                        static_cast<std::size_t>(header.pointsInPacket) * sizeof(PointCloudPoint);
        if (payloadBytes.size() != expectedSize ||
            header.pointsInPacket > maxPointCloudPointsPerPacket ||
            header.packetCount == 0 ||
            header.packetIndex >= header.packetCount ||
            header.pointsInPacket > header.totalPoints)
        {
            return false;
        }

        points.resize(header.pointsInPacket);
        if (!points.empty())
        {
            std::memcpy(points.data(),
                        payloadBytes.data() + sizeof(PayloadPointCloudPacketHeader),
                        points.size() * sizeof(PointCloudPoint));
        }
        return true;
    }

    inline std::string ackMessageToString(const PayloadAck &ack)
    {
        const auto length = std::find(ack.message, ack.message + sizeof(ack.message), '\0') - ack.message;
        return std::string(ack.message, static_cast<std::size_t>(length));
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

                packets.emplace_back(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(packetSize));
                buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(packetSize));
            }

            return packets;
        }

    private:
        std::vector<std::uint8_t> buffer;
    };

} // namespace gcs::protocol