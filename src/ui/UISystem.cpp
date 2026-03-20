#include "ui/UISystem.h"

#include "shared/protocol/protocol_utils.h"

#include <fonts/IconsMaterialSymbols.h>
#include "rendering/RenderingSystem.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include "UISystem.h"

namespace gcs
{

    namespace
    {
        constexpr std::array commandList = 
        {
            protocol::CommandId::PREPARE,
            protocol::CommandId::TAKEOFF,
            protocol::CommandId::START_MISSION,
            protocol::CommandId::PAUSE_RESUME,
            protocol::CommandId::RETURN_HOME,
            protocol::CommandId::LAND,
            protocol::CommandId::EMERGENCY_STOP,
        };

        const char* getCommandLabel(protocol::CommandId commandId, bool isPaused)
        {
            switch (commandId)
            {
            case protocol::CommandId::PREPARE:       return "Подготовка";
            case protocol::CommandId::TAKEOFF:       return "Взлет";
            case protocol::CommandId::START_MISSION: return "Миссия";
            case protocol::CommandId::PAUSE_RESUME:  return isPaused ? "Продолжить" : "Пауза";
            case protocol::CommandId::RETURN_HOME:   return "Домой (RTL)";
            case protocol::CommandId::LAND:          return "Посадка";
            case protocol::CommandId::EMERGENCY_STOP: return "Аварийная остановка";
            default: return "Команда";
            }
        }

        // Иконки для команд
        const char* getCommandIcon(protocol::CommandId commandId, bool isPaused)
        {
            switch (commandId)
            {
                case protocol::CommandId::PREPARE:       return ICON_MS_FACT_CHECK;
                case protocol::CommandId::TAKEOFF:       return ICON_MS_FLIGHT_TAKEOFF;
                case protocol::CommandId::START_MISSION: return ICON_MS_ROUTE;
                case protocol::CommandId::PAUSE_RESUME:  return isPaused ? ICON_MS_PLAY_CIRCLE : ICON_MS_PAUSE;
                case protocol::CommandId::RETURN_HOME:   return ICON_MS_HOME;
                case protocol::CommandId::LAND:          return ICON_MS_FLIGHT_LAND;
                case protocol::CommandId::EMERGENCY_STOP: return ICON_MS_DESTRUCTION;
                default: return ICON_MS_HELP;
            }
        }

        std::size_t commandFeedbackIndex(protocol::CommandId commandId)
        {
            return static_cast<std::size_t>(static_cast<std::uint8_t>(commandId) - 1U);
        }

        std::string formatTimestamp(const std::chrono::system_clock::time_point &timePoint)
        {
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()) % 1000;
            const std::time_t timeValue = std::chrono::system_clock::to_time_t(timePoint);
            std::tm localTime{};
        #ifdef _WIN32
            localtime_s(&localTime, &timeValue);
        #else
            localtime_r(&timeValue, &localTime);
        #endif

            char buffer[32]{};
            std::snprintf(buffer,
                        sizeof(buffer),
                        "%02d:%02d:%02d.%03d",
                        localTime.tm_hour,
                        localTime.tm_min,
                        localTime.tm_sec,
                        static_cast<int>(milliseconds.count()));
            return buffer;
        }

        const char *directionLabel(SharedState::LogDirection direction)
        {
            switch (direction)
            {
            case SharedState::LogDirection::outbound:
                return "->";
            case SharedState::LogDirection::inbound:
                return "<-";
            case SharedState::LogDirection::local:
                return "**";
            }

            return "??";
        }

        ImVec4 stateColor(protocol::DroneState state)
        {
            switch (state)
            {
            case protocol::DroneState::READY:
            case protocol::DroneState::IDLE:
            case protocol::DroneState::IN_FLIGHT:
            case protocol::DroneState::LANDED:
            case protocol::DroneState::EXECUTING_MISSION:
                return ImVec4(0.20f, 0.75f, 0.35f, 1.0f);
            case protocol::DroneState::CONNECTED:
            case protocol::DroneState::PREPARING:
            case protocol::DroneState::ARMING:
            case protocol::DroneState::TAKING_OFF:
            case protocol::DroneState::PAUSED:
            case protocol::DroneState::RETURNING_HOME:
            case protocol::DroneState::LANDING:
                return ImVec4(0.90f, 0.72f, 0.18f, 1.0f);
            case protocol::DroneState::ERROR:
            case protocol::DroneState::EMERGENCY_LANDING:
            case protocol::DroneState::DISCONNECTED:
            default:
                return ImVec4(0.85f, 0.22f, 0.22f, 1.0f);
            }
        }

        void renderStatusIndicator(const char* label, ImVec4 color, ImFont* font)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 screenPosition = ImGui::GetCursorScreenPos();
            float textLineHeight = ImGui::GetTextLineHeight();
            
            // Настройки геометрии
            constexpr float circleRadius = 6.0f;
            const float paddingX = 8.0f;

            // Отрисовка кружка
            ImU32 uColor = ImGui::GetColorU32(color);
            ImVec2 circleCenter = ImVec2(screenPosition.x + circleRadius, screenPosition.y + textLineHeight * 0.5f);
            drawList->AddCircleFilled(circleCenter, circleRadius, uColor);

            // Смещаем курсор для текста (радиус * 2 + отступ)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (circleRadius * 2.0f) + paddingX);

            // Отрисовка текста
            if (font) ImGui::PushFont(font);
            ImGui::TextColored(color, "%s", label);
            if (font) ImGui::PopFont();
        }

        ImVec4 batteryColor(std::uint8_t batteryPercent)
        {
            if (batteryPercent > 50)
            {
                return ImVec4(0.22f, 0.75f, 0.35f, 1.0f);
            }
            if (batteryPercent >= 20)
            {
                return ImVec4(0.92f, 0.72f, 0.18f, 1.0f);
            }
            return ImVec4(0.85f, 0.22f, 0.22f, 1.0f);
        }

        bool shouldShowLog(const SharedState::LogEntry &entry, bool showCommandLogs, bool showTelemetryLogs, bool showErrorLogs)
        {
            switch (entry.category)
            {
            case SharedState::LogCategory::command:
                return showCommandLogs;
            case SharedState::LogCategory::telemetry:
                return showTelemetryLogs;
            case SharedState::LogCategory::error:
                return showErrorLogs;
            }

            return true;
        }

        bool renderIconButton(ImFont *font, const char *icon, ButtonState buttonState, bool isEnabled, float size)
        {
            if (!isEnabled)
                ImGui::BeginDisabled();

            ImGui::PushFont(font, size);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));

            ImVec4 backgroundColor;
            ImVec4 hoverColor;

            switch (buttonState)
            {
            case ButtonState::On:
            {
                backgroundColor = ImVec4(0.23f, 0.60f, 1.0f, 0.60f);
                hoverColor = ImVec4(0.30f, 0.70f, 1.0f, 0.85f);
                break;
            }

            case ButtonState::InProcess:
            {
                float t = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 4.0f); // Пульсация

                backgroundColor = ImVec4(0.25f + t * 0.10f, // R: 0.25 → 0.35
                                        0.25f + t * 0.35f, // G: 0.25 → 0.60
                                        0.25f + t * 0.75f, // B: 0.25 → 1.00
                                        0.50f + t * 0.20f  // A: 0.50 → 0.70
                );
                hoverColor = backgroundColor;
                break;
            }

            case ButtonState::Off:
            default:
            {
                backgroundColor = ImVec4(0.25f, 0.25f, 0.25f, 0.25f);
                hoverColor = ImVec4(0.35f, 0.35f, 0.35f, 0.70f);
                break;
            }
            }

            ImGui::PushStyleColor(ImGuiCol_Button, backgroundColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.45f, 0.85f, 0.9f));

            ImVec2 iconSize = ImGui::CalcTextSize(icon);
            bool clicked = ImGui::Button(icon, ImVec2(iconSize.x + 16, iconSize.y + 16));

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            ImGui::PopFont();

            if (!isEnabled)
                ImGui::EndDisabled();

            return clicked;
        }
    }

    UISystem::UISystem(RenderingSystem &renderingSystemValue,
                    SharedState &sharedStateValue,
                    network::ProtocolClient &protocolClientValue,
                    viewer::PointCloudRenderer &pointCloudRendererValue,
                    const ApplicationPaths &applicationPathsValue)
        : renderingSystem(renderingSystemValue),
        sharedState(sharedStateValue),
        protocolClient(protocolClientValue),
        pointCloudRenderer(pointCloudRendererValue),
        applicationPaths(applicationPathsValue)
    {
    }

    void UISystem::render()
    {
        const SharedState::Snapshot snapshot = sharedState.snapshot();
        renderDockspace();

        // Панели управления
        renderConnectionPanel();
        renderCommandsPanel(snapshot);
        renderMissionParametersPanel(snapshot);
        renderTelemetryPanel(snapshot);
        renderSimulationPanel(snapshot);
        renderLogPanel(snapshot);
        renderPointCloudPanel(snapshot);
    }

    void UISystem::renderDockspace()
    {
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("MainDockspace", nullptr, windowFlags);
        ImGui::PopStyleVar(2);

        ImGui::DockSpace(ImGui::GetID("AtomGCSDockspace"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::End();
    }

    void UISystem::renderConnectionPanel()
    {
        syncConnectionEditor();

        ImGui::Begin("Соединение");

        const auto status = sharedState.getConnectionStatus();
        ImVec4 statusColor;
        if (status == SharedState::ConnectionStatus::connecting)
            statusColor = ImVec4(0.92f, 0.72f, 0.18f, 1.0f);
        else if (status == SharedState::ConnectionStatus::connected)
            statusColor = ImVec4(0.20f, 0.75f, 0.35f, 1.0f);
        else
            statusColor = ImVec4(0.85f, 0.22f, 0.22f, 1.0f);

        renderStatusIndicator(toString(status), statusColor, renderingSystem.getBoldFont());

        ImGui::Spacing();

        // БЛОК КНОПКИ
        if (status == SharedState::ConnectionStatus::disconnected)
        {
            // Кнопка CONNECT (активна только когда отключены)
            if (renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_POWER, ButtonState::Off, true, 85.0f))
            {
                persistConnectionEditor();
                protocolClient.connect();
            }
        }
        else
        {
            // Кнопка DISCONNECT (заблокирована, если в процессе соединения)
            bool canDisconnect = (status != SharedState::ConnectionStatus::connecting);
            if (renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_POWER, ButtonState::On, canDisconnect, 85.0f))
            {
                protocolClient.disconnect();
            }
        }

        ImGui::SameLine();

        const bool lockFields = status != SharedState::ConnectionStatus::disconnected;
        if (ImGui::BeginChild("ConnectionSettings", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX))
        {
            if (lockFields) ImGui::BeginDisabled();

            ImGui::SetNextItemWidth(200);
            if (ImGui::InputText("IP address", ipAddressBuffer.data(), ipAddressBuffer.size()))
                persistConnectionEditor();

            ImGui::SetNextItemWidth(200);
            if (ImGui::InputInt("TCP port", &tcpPortValue)) {
                tcpPortValue = std::clamp(tcpPortValue, 1, 65535);
                persistConnectionEditor();
            }

            ImGui::SetNextItemWidth(200);
            if (ImGui::InputInt("UDP port", &udpPortValue)) {
                udpPortValue = std::clamp(udpPortValue, 1, 65535);
                persistConnectionEditor();
            }

            if (lockFields) ImGui::EndDisabled();
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void UISystem::renderCommandsPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Команды");

        const bool isConnected = snapshot.connectionStatus == SharedState::ConnectionStatus::connected;
        const bool isPaused = static_cast<protocol::DroneState>(snapshot.telemetryState.currentState) == protocol::DroneState::PAUSED;

        if (ImGui::BeginTable("CommandTable", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit))
        {
            for (std::size_t index = 0; index < commandList.size(); ++index)
            {
                const auto commandId = commandList[index];
                const bool commandAvailable = protocol::isCommandAvailable(snapshot.telemetryState.availableCommands, commandId);
                const bool enabled = isConnected && commandAvailable;

                const auto &feedback = snapshot.commandFeedbacks[commandFeedbackIndex(commandId)];
                
                // Определяем визуальное состояние для иконки
                ButtonState btnState = ButtonState::Off;
                if (feedback.pending) btnState = ButtonState::InProcess;
                else if (feedback.active && feedback.result == protocol::AckResult::SUCCESS) btnState = ButtonState::On;

                ImGui::TableNextColumn();
                
                if (renderIconButton(renderingSystem.getMaterialIconsFont(), 
                                     getCommandIcon(commandId, isPaused), 
                                     btnState, enabled, 120.0f))
                {
                    protocolClient.sendCommand(commandId);
                }
            }
            ImGui::EndTable();
        }

        ImGui::TextDisabled("Unavailable commands are disabled from TEL_STATE.available_commands.");
        ImGui::End();
    }

    void UISystem::renderMissionParametersPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Mission Parameters");

        auto missionParameters = snapshot.missionParameters;

        int delayedStartSeconds = static_cast<int>(missionParameters.payload.delayedStartTimeSec);
        if (ImGui::InputInt("Delayed start (sec)", &delayedStartSeconds))
        {
            delayedStartSeconds = std::max(delayedStartSeconds, 0);
            missionParameters.payload.delayedStartTimeSec = static_cast<std::uint32_t>(delayedStartSeconds);
            persistMissionParameters(missionParameters);
        }

        float takeoffAltitude = missionParameters.payload.takeoffAltitudeM;
        if (ImGui::InputFloat("Takeoff altitude (m)", &takeoffAltitude, 0.5f, 1.0f, "%.1f"))
        {
            missionParameters.payload.takeoffAltitudeM = std::clamp(takeoffAltitude, 1.0f, 100.0f);
            persistMissionParameters(missionParameters);
        }

        float flightSpeed = missionParameters.payload.flightSpeedMS;
        if (ImGui::InputFloat("Flight speed (m/s)", &flightSpeed, 0.1f, 0.5f, "%.1f"))
        {
            missionParameters.payload.flightSpeedMS = std::clamp(flightSpeed, 0.5f, 20.0f);
            persistMissionParameters(missionParameters);
        }

        // EXTENSION POINT: add new mission parameter widgets here.
        // EXAMPLE: InputFloat("Max range (m)", &missionParameters.payload.maxRangeM);

        int flightModeIndex = missionParameters.flightMode == protocol::FlightMode::AUTOMATIC ? 0 : 1;
        const char *flightModes[] = {"Automatic", "Semi-automatic"};
        if (ImGui::Combo("Flight mode", &flightModeIndex, flightModes, IM_ARRAYSIZE(flightModes)))
        {
            missionParameters.flightMode =
                flightModeIndex == 0 ? protocol::FlightMode::AUTOMATIC : protocol::FlightMode::SEMI_AUTOMATIC;
            persistMissionParameters(missionParameters);
            protocolClient.sendMode(missionParameters.flightMode);
        }

        if (ImGui::Button("Send parameters"))
        {
            protocolClient.sendMissionParameters(missionParameters);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save to file"))
        {
            std::string errorMessage;
            if (config::saveMissionParameters(applicationPaths.missionParametersFile, missionParameters, errorMessage))
            {
                sharedState.appendLog(SharedState::LogDirection::local,
                                    SharedState::LogCategory::command,
                                    "MISSION",
                                    "Mission parameters saved to file");
            }
            else
            {
                sharedState.appendLog(SharedState::LogDirection::local,
                                    SharedState::LogCategory::error,
                                    "MISSION",
                                    "Save failed: " + errorMessage);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load from file"))
        {
            SharedState::MissionParametersModel loadedMissionParameters = missionParameters;
            std::string errorMessage;
            if (config::loadMissionParameters(applicationPaths.missionParametersFile, loadedMissionParameters, errorMessage))
            {
                persistMissionParameters(loadedMissionParameters);
                sharedState.appendLog(SharedState::LogDirection::local,
                                    SharedState::LogCategory::command,
                                    "MISSION",
                                    "Mission parameters loaded from file");
            }
            else
            {
                sharedState.appendLog(SharedState::LogDirection::local,
                                    SharedState::LogCategory::error,
                                    "MISSION",
                                    "Load failed: " + errorMessage);
            }
        }

        ImGui::End();
    }

    void UISystem::renderTelemetryPanel(const SharedState::Snapshot &snapshot) const
    {
        ImGui::Begin("Telemetry");

        const auto droneState = static_cast<protocol::DroneState>(snapshot.telemetryState.currentState);
        const ImVec4 indicatorColor = stateColor(droneState);

        renderStatusIndicator(protocol::droneStateToString(droneState), indicatorColor, renderingSystem.getBoldFont());

        ImGui::Text("Mode: %s",
                    protocol::flightModeToString(static_cast<protocol::FlightMode>(snapshot.telemetryState.flightMode)));
        ImGui::Separator();

        ImGui::Text("Position X: %.2f m", snapshot.telemetryPosition.posX);
        ImGui::Text("Position Y: %.2f m", snapshot.telemetryPosition.posY);
        ImGui::Text("Position Z: %.2f m", snapshot.telemetryPosition.posZ);
        ImGui::Separator();
        ImGui::Text("Velocity X: %.2f m/s", snapshot.telemetryPosition.velX);
        ImGui::Text("Velocity Y: %.2f m/s", snapshot.telemetryPosition.velY);
        ImGui::Text("Velocity Z: %.2f m/s", snapshot.telemetryPosition.velZ);
        ImGui::Separator();
        ImGui::Text("Altitude AGL: %.2f m", snapshot.telemetryPosition.altitudeAglM);
        ImGui::Text("Heading: %.1f deg", snapshot.telemetryPosition.headingDeg);

        ImGui::Separator();
        ImGui::Text("Battery");
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, batteryColor(snapshot.telemetryState.batteryPercent));
        ImGui::ProgressBar(snapshot.telemetryState.batteryPercent / 100.0f,
                        ImVec2(-1.0f, 0.0f),
                        (std::to_string(snapshot.telemetryState.batteryPercent) + "%").c_str());
        ImGui::PopStyleColor();

        ImGui::End();
    }

    void UISystem::renderSimulationPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Simulation");

        auto simulation = snapshot.simulation;
        bool obstaclesChanged = false;

        constexpr float cellSize = 54.0f;
        constexpr float cellSpacing = 6.0f;
        const ImVec2 gridSize(cellSize * 3.0f + cellSpacing * 2.0f, cellSize * 3.0f + cellSpacing * 2.0f);
        const ImVec2 gridOrigin = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton("ObstacleGrid", gridSize);
        ImDrawList *drawList = ImGui::GetWindowDrawList();

        const auto drawCell = [&](int row, int column, ImU32 fillColor, const char *label) {
            const ImVec2 min(gridOrigin.x + column * (cellSize + cellSpacing),
                            gridOrigin.y + row * (cellSize + cellSpacing));
            const ImVec2 max(min.x + cellSize, min.y + cellSize);
            drawList->AddRectFilled(min, max, fillColor, 8.0f);
            drawList->AddRect(min, max, IM_COL32(90, 95, 110, 255), 8.0f, 0, 1.5f);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            drawList->AddText(ImVec2(min.x + (cellSize - textSize.x) * 0.5f, min.y + (cellSize - textSize.y) * 0.5f),
                            IM_COL32(235, 240, 245, 255),
                            label);
        };

        drawCell(0, 0, simulation.obstacles.frontLeft ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "FL");
        drawCell(0, 1, simulation.obstacles.front ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "F");
        drawCell(0, 2, simulation.obstacles.frontRight ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "FR");
        drawCell(1, 0, simulation.obstacles.left ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "L");

        const auto verticalState = static_cast<protocol::VerticalObstacle>(simulation.obstacles.vertical);
        const char *centerLabel = "DRN";
        ImU32 centerColor = IM_COL32(70, 78, 90, 255);
        if (verticalState == protocol::VerticalObstacle::ABOVE)
        {
            centerLabel = "UP";
            centerColor = IM_COL32(220, 140, 50, 255);
        }
        else if (verticalState == protocol::VerticalObstacle::BELOW)
        {
            centerLabel = "DN";
            centerColor = IM_COL32(220, 140, 50, 255);
        }
        drawCell(1, 1, centerColor, centerLabel);

        drawCell(1, 2, simulation.obstacles.right ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "R");
        drawCell(2, 0, simulation.obstacles.backLeft ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "BL");
        drawCell(2, 1, simulation.obstacles.back ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "B");
        drawCell(2, 2, simulation.obstacles.backRight ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "BR");

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const ImVec2 mousePosition = ImGui::GetIO().MousePos;
            const float localX = mousePosition.x - gridOrigin.x;
            const float localY = mousePosition.y - gridOrigin.y;
            const int column = static_cast<int>(localX / (cellSize + cellSpacing));
            const int row = static_cast<int>(localY / (cellSize + cellSpacing));

            if (column >= 0 && column < 3 && row >= 0 && row < 3)
            {
                if (row == 0 && column == 0)
                {
                    simulation.obstacles.frontLeft = !simulation.obstacles.frontLeft;
                }
                else if (row == 0 && column == 1)
                {
                    simulation.obstacles.front = !simulation.obstacles.front;
                }
                else if (row == 0 && column == 2)
                {
                    simulation.obstacles.frontRight = !simulation.obstacles.frontRight;
                }
                else if (row == 1 && column == 0)
                {
                    simulation.obstacles.left = !simulation.obstacles.left;
                }
                else if (row == 1 && column == 1)
                {
                    if (verticalState == protocol::VerticalObstacle::NONE)
                    {
                        simulation.obstacles.vertical = static_cast<std::uint8_t>(protocol::VerticalObstacle::ABOVE);
                    }
                    else if (verticalState == protocol::VerticalObstacle::ABOVE)
                    {
                        simulation.obstacles.vertical = static_cast<std::uint8_t>(protocol::VerticalObstacle::BELOW);
                    }
                    else
                    {
                        simulation.obstacles.vertical = static_cast<std::uint8_t>(protocol::VerticalObstacle::NONE);
                    }
                }
                else if (row == 1 && column == 2)
                {
                    simulation.obstacles.right = !simulation.obstacles.right;
                }
                else if (row == 2 && column == 0)
                {
                    simulation.obstacles.backLeft = !simulation.obstacles.backLeft;
                }
                else if (row == 2 && column == 1)
                {
                    simulation.obstacles.back = !simulation.obstacles.back;
                }
                else if (row == 2 && column == 2)
                {
                    simulation.obstacles.backRight = !simulation.obstacles.backRight;
                }
                obstaclesChanged = true;
            }
        }

        if (obstaclesChanged)
        {
            sharedState.setSimulationModel(simulation);
            protocolClient.sendSimulationObstacles(simulation.obstacles);
        }

        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        bool lidarActive = simulation.lidar.lidarActive;
        if (ImGui::Checkbox("LiDAR active", &lidarActive))
        {
            simulation.lidar.lidarActive = lidarActive;
            sharedState.setSimulationModel(simulation);
            protocolClient.sendSimulationLidar(lidarActive);
        }

        ImGui::End();
    }

    void UISystem::renderLogPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Log");

        ImGui::Checkbox("Commands", &showCommandLogs);
        ImGui::SameLine();
        ImGui::Checkbox("Telemetry", &showTelemetryLogs);
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &showErrorLogs);

        auto preferences = snapshot.uiPreferences;
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto-scroll", &preferences.autoScrollLog))
        {
            sharedState.setUiPreferences(preferences);
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            sharedState.clearLogs();
        }

        ImGui::Separator();
        ImGui::BeginChild("LogEntries");
        for (const auto &entry : snapshot.logs)
        {
            if (!shouldShowLog(entry, showCommandLogs, showTelemetryLogs, showErrorLogs))
            {
                continue;
            }

            std::ostringstream line;
            line << '[' << formatTimestamp(entry.timestamp) << "] [" << directionLabel(entry.direction) << "] ["
                << entry.type << "] " << entry.description;
            ImGui::TextUnformatted(line.str().c_str());
        }

        if (preferences.autoScrollLog)
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void UISystem::renderPointCloudPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Point Cloud");

        auto preferences = snapshot.uiPreferences;
        if (ImGui::SliderFloat("Point size", &preferences.pointSize, 1.0f, 8.0f, "%.1f"))
        {
            sharedState.setUiPreferences(preferences);
        }

        int colorModeIndex = preferences.colorMode == SharedState::ColorMode::intensity ? 0 : 1;
        const char *colorModes[] = {"Intensity", "Distance"};
        if (ImGui::Combo("Color mode", &colorModeIndex, colorModes, IM_ARRAYSIZE(colorModes)))
        {
            preferences.colorMode = colorModeIndex == 0 ? SharedState::ColorMode::intensity : SharedState::ColorMode::distance;
            sharedState.setUiPreferences(preferences);
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset camera"))
        {
            pointCloudRenderer.getCamera().reset();
        }

        ImGui::Text("Frame: %u, points: %zu", snapshot.pointCloud.timestampMs, snapshot.pointCloud.points.size());
        ImGui::Separator();

        ImVec2 viewerSize = ImGui::GetContentRegionAvail();
        viewerSize.x = std::max(viewerSize.x, 200.0f);
        viewerSize.y = std::max(viewerSize.y, 240.0f);

        pointCloudRenderer.render(snapshot.pointCloud, preferences, viewerSize);
        ImGui::Image(pointCloudRenderer.getTextureRef(), viewerSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

        const bool hovered = ImGui::IsItemHovered();
        pointCloudRenderer.getCamera().handleInput(hovered, viewerSize);

        const ImVec2 imageMin = ImGui::GetItemRectMin();
        pointCloudRenderer.drawAxisOverlay(ImGui::GetWindowDrawList(), ImVec2(imageMin.x + 32.0f, imageMin.y + 36.0f), 20.0f);

        ImGui::TextDisabled("LMB: orbit  |  MMB: pan  |  Wheel: zoom");
        ImGui::End();
    }
    
    void UISystem::syncConnectionEditor()
    {
        if (connectionEditorInitialized)
        {
            return;
        }

        const auto settings = sharedState.getConnectionSettings();
        ipAddressBuffer.fill('\0');
        const auto copyLength = std::min(ipAddressBuffer.size() - 1, settings.ipAddress.size());
        std::memcpy(ipAddressBuffer.data(), settings.ipAddress.data(), copyLength);
        tcpPortValue = settings.tcpPort;
        udpPortValue = settings.udpPort;
        connectionEditorInitialized = true;
    }

    void UISystem::persistConnectionEditor() const
    {
        SharedState::ConnectionSettings settings;
        settings.ipAddress = ipAddressBuffer.data();
        settings.tcpPort = static_cast<std::uint16_t>(std::clamp(tcpPortValue, 1, 65535));
        settings.udpPort = static_cast<std::uint16_t>(std::clamp(udpPortValue, 1, 65535));
        sharedState.setConnectionSettings(std::move(settings));
    }

    void UISystem::persistMissionParameters(const SharedState::MissionParametersModel &missionParameters) const
    {
        sharedState.setMissionParameters(missionParameters);
    }

    const char *UISystem::commandButtonLabel(protocol::CommandId commandId, bool isPaused)
    {
        switch (commandId)
        {
        case protocol::CommandId::PREPARE:
            return "Prepare";
        case protocol::CommandId::TAKEOFF:
            return "Takeoff";
        case protocol::CommandId::START_MISSION:
            return "Start mission";
        case protocol::CommandId::PAUSE_RESUME:
            return isPaused ? "Resume" : "Pause";
        case protocol::CommandId::RETURN_HOME:
            return "Return home";
        case protocol::CommandId::LAND:
            return "Land";
        case protocol::CommandId::EMERGENCY_STOP:
            return "Emergency stop";
        }

        return "Command";
    }

    ImVec4 UISystem::commandButtonColor(const SharedState::CommandFeedback &feedback)
    {
        const auto now = std::chrono::steady_clock::now();
        if (feedback.pending)
        {
            const auto elapsed = now - feedback.sentAt;
            if (elapsed <= std::chrono::seconds(2))
            {
                return ImVec4(0.92f, 0.74f, 0.18f, 0.90f);
            }
            return ImVec4(0.85f, 0.22f, 0.22f, 0.90f);
        }

        switch (feedback.result)
        {
        case protocol::AckResult::SUCCESS:
            return ImVec4(0.20f, 0.72f, 0.34f, 0.90f);
        case protocol::AckResult::REJECTED:
        case protocol::AckResult::INVALID_PARAM:
        case protocol::AckResult::ERROR:
            return ImVec4(0.85f, 0.22f, 0.22f, 0.90f);
        }

        return ImVec4(0.35f, 0.35f, 0.35f, 0.90f);
    }

    bool UISystem::isCommandFeedbackVisible(const SharedState::CommandFeedback &feedback)
    {
        if (!feedback.active)
        {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        if (feedback.pending)
        {
            return now - feedback.sentAt <= std::chrono::milliseconds(2500);
        }

        return now - feedback.updatedAt <= std::chrono::milliseconds(500);
    }

}