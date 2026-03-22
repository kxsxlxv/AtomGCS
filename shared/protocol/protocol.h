#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

/*
Пример бинарного пакета

Байт:   0     1     2     3     4     5     6     7
       [0xAA][0x55][0x01][0x01][0x01][0x00][0x02][CRC]
        ╰magic─╯   vers  type  ╰─len=1──╯   cmd   crc
                          ↑                  ↑
                     CMD_COMMAND         TAKEOFF
*/

namespace gcs::protocol
{

    constexpr std::array<std::uint8_t, 2> packetMagic{0xAA, 0x55};
    constexpr std::uint8_t protocolVersion = 0x01;

    #pragma pack(push, 1)

    /*
    Типы сообщений

    Правило именования:
    CMD_* (0x01–0x7F) — команды НСУ -> МУП
    TEL_* (0x81–0xFF) — телеметрия МУП -> НСУ
    */
    enum class MsgType : std::uint8_t
    {
        // НСУ -> МУП
        CMD_COMMAND = 0x01,
        CMD_SET_PARAMS = 0x02,
        CMD_SET_MODE = 0x03,
        CMD_SIM_OBSTACLES = 0x04,
        CMD_SIM_LIDAR = 0x05,

        // МУП -> НСУ
        TEL_STATE = 0x81,
        TEL_POSITION = 0x82,
        TEL_POINT_CLOUD = 0x83,
        TEL_ACK = 0x84,
    };

    struct PacketHeader
    {
        std::uint8_t magic[2]; // Фиксированные байты {0xAA, 0x55}. Нужны для поиска начала пакета в потоке байт. Приёмник ищет эту последовательность, чтобы понять где начинается пакет
        std::uint8_t version; // Номер версии протокола. Приёмник может отклонить пакет с неизвестной версией
        MsgType msgType; // Определяет, какая структура лежит в payload
        std::uint16_t payloadLen; //Длина payload в байтах (0–65535). Записывается в формате little-endian: сначала младший байт, потом старший
    };

    enum class CommandId : std::uint8_t
    {
        PREPARE = 0x01,
        TAKEOFF = 0x02,
        START_MISSION = 0x03,
        PAUSE_RESUME = 0x04,
        RETURN_HOME = 0x05,
        LAND = 0x06,
        EMERGENCY_STOP = 0x07,
    };

    struct PayloadCommand
    {
        CommandId commandId;
    };

    struct PayloadMissionParams
    {
        std::uint32_t delayedStartTimeSec;
        float takeoffAltitudeM;
        float flightSpeedMS;
    };

    enum class FlightMode : std::uint8_t
    {
        AUTOMATIC = 0x01,
        SEMI_AUTOMATIC = 0x02
    };

    struct PayloadSetMode
    {
        FlightMode mode;
    };

    enum class VerticalObstacle : std::uint8_t
    {
        NONE = 0,
        ABOVE = 1,
        BELOW = 2,
    };

    struct PayloadSimObstacles
    {
        std::uint8_t front;
        std::uint8_t frontRight;
        std::uint8_t right;
        std::uint8_t backRight;
        std::uint8_t back;
        std::uint8_t backLeft;
        std::uint8_t left;
        std::uint8_t frontLeft;
        VerticalObstacle vertical;
    };

    struct PayloadSimLidar
    {
        std::uint8_t lidarActive;
    };

    enum class DroneState : std::uint8_t
    {
        DISCONNECTED = 0x00,
        CONNECTED = 0x01,
        IDLE = 0x02,
        PREPARING = 0x03,
        READY = 0x04,
        ARMING = 0x05,
        TAKING_OFF = 0x06,
        IN_FLIGHT = 0x07,
        EXECUTING_MISSION = 0x08,
        PAUSED = 0x09,
        RETURNING_HOME = 0x0A,
        LANDING = 0x0B,
        LANDED = 0x0C,
        INTERNAL_ERROR = 0x0D,
        EMERGENCY_LANDING = 0x0E,
    };

    struct PayloadTelemetryState
    {
        DroneState currentState;
        std::uint32_t availableCommands; // Битовая маска доступных команд
        FlightMode flightMode;
        std::uint8_t batteryPercent;
    };

    struct PayloadTelemetryPosition
    {
        float posX, posY, posZ; // Координаты, метры
        float velX, velY, velZ; // Скорости, м/с
        float headingDeg; // Курс
        float altitudeAglM; // Высота над землёй (AGL)
    };

    struct PointCloudPoint
    {
        float x;
        float y;
        float z;
        std::uint8_t intensity;
    };

    struct PayloadPointCloudHeader
    {
        std::uint32_t timestampMs;
        std::uint32_t numPoints;
    };

    enum class AckResult : std::uint8_t
    {
        SUCCESS = 0x00, // Команда принята и выполняется
        REJECTED = 0x01, // Команда отклонена
        INVALID_PARAM = 0x02, // Неверные параметры
        INTERNAL_ERROR = 0x03 // Внутренняя ошибка модуля
    };

    struct PayloadAck
    {
        MsgType originalMsgType;
        CommandId originalCommandId;
        AckResult result;
        char message[64];
    };

    #pragma pack(pop)

    // проверка размеров
    static_assert(sizeof(PacketHeader) == 6);
    static_assert(sizeof(PayloadCommand) == 1);
    static_assert(sizeof(PayloadMissionParams) == 12);
    static_assert(sizeof(PayloadSetMode) == 1);
    static_assert(sizeof(PayloadSimObstacles) == 9);
    static_assert(sizeof(PayloadSimLidar) == 1);
    static_assert(sizeof(PayloadTelemetryState) == 7);
    static_assert(sizeof(PayloadTelemetryPosition) == 32);
    static_assert(sizeof(PointCloudPoint) == 13);
    static_assert(sizeof(PayloadPointCloudHeader) == 8);
    static_assert(sizeof(PayloadAck) == 67);

    // защита от случайного изменения структур
    static_assert(std::is_trivially_copyable_v<PacketHeader>);
    static_assert(std::is_trivially_copyable_v<PayloadCommand>);
    static_assert(std::is_trivially_copyable_v<PayloadMissionParams>);
    static_assert(std::is_trivially_copyable_v<PayloadSetMode>);
    static_assert(std::is_trivially_copyable_v<PayloadSimObstacles>);
    static_assert(std::is_trivially_copyable_v<PayloadSimLidar>);
    static_assert(std::is_trivially_copyable_v<PayloadTelemetryState>);
    static_assert(std::is_trivially_copyable_v<PayloadTelemetryPosition>);
    static_assert(std::is_trivially_copyable_v<PointCloudPoint>);
    static_assert(std::is_trivially_copyable_v<PayloadPointCloudHeader>);
    static_assert(std::is_trivially_copyable_v<PayloadAck>);

}