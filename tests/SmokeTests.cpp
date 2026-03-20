#include "config/AppConfig.h"
#include "shared/protocol/protocol_utils.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Payload>
void verifyPayloadRoundTrip(gcs::protocol::MsgType msgType, const Payload &payload)
{
    const auto packet = gcs::protocol::serializePacket(msgType, payload);
    require(!packet.empty(), "serializePacket returned empty payload");

    auto parsed = gcs::protocol::tryParsePacket(packet);
    require(parsed.has_value(), "Packet did not parse after serialization");
    require(parsed->msgType == msgType, "Parsed message type mismatch");

    Payload parsedPayload{};
    require(gcs::protocol::parsePayload(parsed->payload, parsedPayload), "Payload parse failed");
    require(std::memcmp(&payload, &parsedPayload, sizeof(Payload)) == 0, "Payload mismatch after roundtrip");
}

void testPayloadRoundTrips()
{
    using namespace gcs::protocol;

    verifyPayloadRoundTrip(MsgType::CMD_COMMAND, PayloadCommand{static_cast<std::uint8_t>(CommandId::TAKEOFF)});
    verifyPayloadRoundTrip(MsgType::CMD_SET_PARAMS, PayloadMissionParams{42U, 15.0f, 7.5f});
    verifyPayloadRoundTrip(MsgType::CMD_SET_MODE, PayloadSetMode{static_cast<std::uint8_t>(FlightMode::SEMI_AUTOMATIC)});
    verifyPayloadRoundTrip(MsgType::CMD_SIM_OBSTACLES,
                           PayloadSimObstacles{true, false, true, false, true, false, true, false,
                                               static_cast<std::uint8_t>(VerticalObstacle::ABOVE)});
    verifyPayloadRoundTrip(MsgType::CMD_SIM_LIDAR, PayloadSimLidar{true});
    verifyPayloadRoundTrip(MsgType::TEL_STATE,
                           PayloadTelemetryState{static_cast<std::uint8_t>(DroneState::READY),
                                                 commandBit(CommandId::PREPARE) | commandBit(CommandId::LAND),
                                                 static_cast<std::uint8_t>(FlightMode::AUTOMATIC),
                                                 76U});
    verifyPayloadRoundTrip(MsgType::TEL_POSITION,
                           PayloadTelemetryPosition{1.0f, 2.0f, -3.0f, 0.1f, 0.2f, 0.3f, 180.0f, 12.0f});

    PayloadAck ack{};
    ack.originalMsgType = static_cast<std::uint8_t>(MsgType::CMD_COMMAND);
    ack.originalCommandId = static_cast<std::uint8_t>(CommandId::PREPARE);
    ack.result = static_cast<std::uint8_t>(AckResult::SUCCESS);
    std::strncpy(ack.message, "OK", sizeof(ack.message) - 1);
    verifyPayloadRoundTrip(MsgType::TEL_ACK, ack);
}

void testCrcMismatch()
{
    auto packet = gcs::protocol::serializePacket(gcs::protocol::MsgType::CMD_SIM_LIDAR,
                                                     gcs::protocol::PayloadSimLidar{true});
    require(!packet.empty(), "Packet for CRC test is empty");
    packet.back() ^= 0xFFU;

    gcs::protocol::PacketParseError error = gcs::protocol::PacketParseError::None;
    const auto parsed = gcs::protocol::tryParsePacket(packet, &error);
    require(!parsed.has_value(), "CRC mismatch packet parsed successfully");
    require(error == gcs::protocol::PacketParseError::CrcMismatch, "CRC mismatch error not reported");
}

void testWrongVersion()
{
    auto packet = gcs::protocol::serializePacket(gcs::protocol::MsgType::CMD_SIM_LIDAR,
                                                     gcs::protocol::PayloadSimLidar{true});
    require(!packet.empty(), "Packet for version test is empty");
    packet[2] = 0x55U;

    gcs::protocol::PacketParseError error = gcs::protocol::PacketParseError::None;
    const auto parsed = gcs::protocol::tryParsePacket(packet, &error);
    require(!parsed.has_value(), "Wrong-version packet parsed successfully");
    require(error == gcs::protocol::PacketParseError::InvalidVersion, "Wrong-version error not reported");
}

void testMagicResyncAndPartialFrames()
{
    using namespace gcs::protocol;
    PacketStreamParser parser;

    const auto firstPacket = serializePacket(MsgType::CMD_SET_MODE,
                                             PayloadSetMode{static_cast<std::uint8_t>(FlightMode::AUTOMATIC)});
    const auto secondPacket = serializePacket(MsgType::CMD_SIM_LIDAR, PayloadSimLidar{false});

    std::vector<std::uint8_t> noisyStream = {0x00U, 0x11U, 0x22U, 0x33U};
    noisyStream.insert(noisyStream.end(), firstPacket.begin(), firstPacket.begin() + firstPacket.size() / 2);
    parser.append(noisyStream);
    require(parser.extractPackets().empty(), "Parser extracted packet from incomplete stream");

    parser.append(std::span<const std::uint8_t>(firstPacket.data() + firstPacket.size() / 2,
                                                firstPacket.size() - firstPacket.size() / 2));
    auto extracted = parser.extractPackets();
    require(extracted.size() == 1, "Parser failed to resync and emit the first packet");

    parser.append(secondPacket);
    extracted = parser.extractPackets();
    require(extracted.size() == 1, "Parser failed to extract the second packet");
}

void testAckMessageHelper()
{
    gcs::protocol::PayloadAck ack{};
    std::strncpy(ack.message, "Rejected by module", sizeof(ack.message) - 1);
    require(gcs::protocol::ackMessageToString(ack) == "Rejected by module", "ACK message helper failed");
}

void testJsonConfigRoundTrip()
{
    namespace fs = std::filesystem;

    const fs::path tempDirectory = fs::temp_directory_path() / "atomgcs_smoke_tests";
    fs::create_directories(tempDirectory);
    const fs::path appConfigPath = tempDirectory / "app_config.json";
    const fs::path missionPath = tempDirectory / "mission_params.json";

    gcs::config::AppConfigModel appConfig;
    appConfig.connectionSettings.ipAddress = "192.168.0.20";
    appConfig.connectionSettings.tcpPort = 6000;
    appConfig.connectionSettings.udpPort = 6001;
    appConfig.uiPreferences.pointSize = 4.5f;
    appConfig.uiPreferences.colorMode = gcs::SharedState::ColorMode::distance;
    appConfig.uiPreferences.logBufferSize = 256;
    appConfig.uiPreferences.autoScrollLog = false;

    std::string errorMessage;
    require(gcs::config::saveAppConfig(appConfigPath, appConfig, errorMessage), "Failed to save app config");

    gcs::config::AppConfigModel loadedAppConfig;
    require(gcs::config::loadAppConfig(appConfigPath, loadedAppConfig, errorMessage), "Failed to load app config");
    require(loadedAppConfig.connectionSettings.ipAddress == appConfig.connectionSettings.ipAddress,
            "App config IP mismatch");
    require(loadedAppConfig.connectionSettings.tcpPort == appConfig.connectionSettings.tcpPort,
            "App config TCP port mismatch");
    require(loadedAppConfig.uiPreferences.colorMode == appConfig.uiPreferences.colorMode,
            "App config color mode mismatch");

    gcs::SharedState::MissionParametersModel missionParameters;
    missionParameters.payload.delayedStartTimeSec = 9U;
    missionParameters.payload.takeoffAltitudeM = 17.0f;
    missionParameters.payload.flightSpeedMS = 3.5f;
    missionParameters.flightMode = gcs::protocol::FlightMode::SEMI_AUTOMATIC;

    require(gcs::config::saveMissionParameters(missionPath, missionParameters, errorMessage),
            "Failed to save mission parameters");

    gcs::SharedState::MissionParametersModel loadedMissionParameters;
    require(gcs::config::loadMissionParameters(missionPath, loadedMissionParameters, errorMessage),
            "Failed to load mission parameters");
    require(loadedMissionParameters.payload.delayedStartTimeSec == missionParameters.payload.delayedStartTimeSec,
            "Mission delayed start mismatch");
    require(loadedMissionParameters.payload.takeoffAltitudeM == missionParameters.payload.takeoffAltitudeM,
            "Mission altitude mismatch");
    require(loadedMissionParameters.flightMode == missionParameters.flightMode, "Mission flight mode mismatch");

    fs::remove_all(tempDirectory);
}

void testCommandBitmaskHelpers()
{
    using namespace gcs::protocol;
    const std::uint32_t mask = commandBit(CommandId::PREPARE) | commandBit(CommandId::LAND);
    require(isCommandAvailable(mask, CommandId::PREPARE), "PREPARE command bit missing");
    require(isCommandAvailable(mask, CommandId::LAND), "LAND command bit missing");
    require(!isCommandAvailable(mask, CommandId::TAKEOFF), "TAKEOFF command bit unexpectedly present");
}
} // namespace

int main()
{
    try
    {
        testPayloadRoundTrips();
        testCrcMismatch();
        testWrongVersion();
        testMagicResyncAndPartialFrames();
        testAckMessageHelper();
        testJsonConfigRoundTrip();
        testCommandBitmaskHelpers();
        std::cout << "Smoke tests passed" << std::endl;
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Smoke tests failed: " << exception.what() << std::endl;
        return 1;
    }
}
