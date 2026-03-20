#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

namespace gcs::protocol
{

#ifdef ERROR
#undef ERROR
#endif

constexpr std::array<std::uint8_t, 2> packetMagic{0xAA, 0x55};
constexpr std::uint8_t protocolVersion = 0x01;

#pragma pack(push, 1)

struct PacketHeader
{
    std::uint8_t magic[2];
    std::uint8_t version;
    std::uint8_t msgType;
    std::uint16_t payloadLen;
};

enum class MsgType : std::uint8_t
{
    CMD_COMMAND = 0x01,
    CMD_SET_PARAMS = 0x02,
    CMD_SET_MODE = 0x03,
    CMD_SIM_OBSTACLES = 0x04,
    CMD_SIM_LIDAR = 0x05,
    // EXTENSION POINT: add new UI -> module message types here.
    // EXAMPLE: CMD_UPLOAD_WAYPOINTS = 0x06,

    TEL_STATE = 0x81,
    TEL_POSITION = 0x82,
    TEL_POINT_CLOUD = 0x83,
    TEL_ACK = 0x84,
    // EXTENSION POINT: add new module -> UI message types here.
    // EXAMPLE: TEL_DEBUG_TEXT = 0x85,
};

enum class CommandId : std::uint8_t
{
    PREPARE = 0x01,
    TAKEOFF = 0x02,
    START_MISSION = 0x03,
    PAUSE_RESUME = 0x04,
    RETURN_HOME = 0x05,
    LAND = 0x06,
    // EXTENSION POINT: add new commands here.
    // EXAMPLE: HOLD_POSITION = 0x07,
    EMERGENCY_STOP = 0x07,
};

struct PayloadCommand
{
    std::uint8_t commandId;
};

struct PayloadMissionParams
{
    std::uint32_t delayedStartTimeSec;
    float takeoffAltitudeM;
    float flightSpeedMS;
    // добавьте сюда дополнительные параметры миссии
    // пример: float maxRangeM;
};

enum class FlightMode : std::uint8_t
{
    AUTOMATIC = 0x01,
    SEMI_AUTOMATIC = 0x02,
    // добавьте сюда дополнительные режимы
};

struct PayloadSetMode
{
    std::uint8_t mode;
};

enum class VerticalObstacle : std::uint8_t
{
    NONE = 0,
    ABOVE = 1,
    BELOW = 2,
};

struct PayloadSimObstacles
{
    bool front;
    bool frontRight;
    bool right;
    bool backRight;
    bool back;
    bool backLeft;
    bool left;
    bool frontLeft;
    std::uint8_t vertical;
};

struct PayloadSimLidar
{
    bool lidarActive;
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
    ERROR = 0x0D,
    EMERGENCY_LANDING = 0x0E,
    // добавьте сюда дополнительные поля состояния дрона
};

struct PayloadTelemetryState
{
    std::uint8_t currentState;
    std::uint32_t availableCommands;
    std::uint8_t flightMode;
    std::uint8_t batteryPercent;
    // добавьте сюда дополнительные поля состояния телеметри
};

struct PayloadTelemetryPosition
{
    float posX;
    float posY;
    float posZ;
    float velX;
    float velY;
    float velZ;
    float headingDeg;
    float altitudeAglM;
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
    SUCCESS = 0x00,
    REJECTED = 0x01,
    INVALID_PARAM = 0x02,
    ERROR = 0x03,
};

struct PayloadAck
{
    std::uint8_t originalMsgType;
    std::uint8_t originalCommandId;
    std::uint8_t result;
    char message[64];
};

#pragma pack(pop)

static_assert(sizeof(bool) == 1, "Protocol assumes bool occupies exactly one byte.");
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