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

#include <glm/trigonometric.hpp>
#include "viewer/SceneOverlay.h"
namespace gcs
{

    namespace
    {
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

        // Подсказки
        const char *getCommandTooltip(protocol::CommandId commandId, bool isPaused)
        {
            switch (commandId)
            {
            case protocol::CommandId::PREPARE:        return "Подготовка дрона к полёту";
            case protocol::CommandId::TAKEOFF:        return "Взлёт на заданную высоту";
            case protocol::CommandId::START_MISSION:  return "Начать миссию";
            case protocol::CommandId::PAUSE_RESUME:   return isPaused ? "Продолжить миссию" : "Приостановить миссию";
            case protocol::CommandId::RETURN_HOME:    return "Возврат на точку старта";
            case protocol::CommandId::LAND:           return "Посадка на текущую позицию";
            case protocol::CommandId::EMERGENCY_STOP: return "Аварийная остановка двигателей";
            default: return "Неизвестная команда";
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
            case protocol::DroneState::INTERNAL_ERROR:
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

        CommandButtonState feedbackToButtonState(const SharedState::CommandFeedback &feedback)
        {
            if (feedback.pending)
                return CommandButtonState::Pending;

            return CommandButtonState::Idle;
        }

        bool renderIconButton(ImFont *font, const char *icon, CommandButtonState state, bool isEnabled, float size)
        {
            if (!isEnabled)
                ImGui::BeginDisabled();

            ImGui::PushFont(font, size);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));

            ImVec4 backgroundColor;
            ImVec4 hoverColor;
            ImVec4 activeColor = ImVec4(0.15f, 0.45f, 0.85f, 0.9f);

            if (state == CommandButtonState::Pending)
            {
                float t = 0.5f + 0.5f * sinf(static_cast<float>(ImGui::GetTime()) * 4.0f);
                backgroundColor = ImVec4(
                    0.25f + t * 0.10f,
                    0.25f + t * 0.35f,
                    0.25f + t * 0.75f,
                    0.50f + t * 0.20f);
                hoverColor  = backgroundColor;
                activeColor = backgroundColor;
            }
            else
            {
                backgroundColor = ImVec4(0.25f, 0.25f, 0.25f, 0.25f);
                hoverColor      = ImVec4(0.35f, 0.35f, 0.35f, 0.70f);
                activeColor     = ImVec4(0.15f, 0.45f, 0.85f, 0.90f);
            }

            ImGui::PushStyleColor(ImGuiCol_Button,        backgroundColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   activeColor);

            ImVec2 iconSize = ImGui::CalcTextSize(icon);
            bool clicked = ImGui::Button(icon, ImVec2(iconSize.x + 16, iconSize.y + 16));

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            ImGui::PopFont();

            if (!isEnabled)
                ImGui::EndDisabled();

            return clicked;
        }

        bool renderEmergencyButton(ImFont *font, const char *icon, CommandButtonState state, bool isEnabled, float size)
        {
            if (!isEnabled)
                ImGui::BeginDisabled();

            ImGui::PushFont(font, size);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));

            ImVec4 backgroundColor, hoverColor, activeColor;

            if (state == CommandButtonState::Pending)
            {
                float t = 0.5f + 0.5f * sinf(static_cast<float>(ImGui::GetTime()) * 4.0f);
                backgroundColor = ImVec4(0.70f + t * 0.15f, 0.10f, 0.10f, 0.70f + t * 0.20f);
                hoverColor = backgroundColor;
                activeColor = backgroundColor;
            }
            else
            {
                backgroundColor = ImVec4(0.70f, 0.12f, 0.12f, 0.80f);
                hoverColor = ImVec4(0.85f, 0.18f, 0.18f, 0.90f);
                activeColor = ImVec4(1.00f, 0.10f, 0.10f, 1.00f);
            }

            ImGui::PushStyleColor(ImGuiCol_Button,        backgroundColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   activeColor);

            ImVec2 iconSize = ImGui::CalcTextSize(icon);
            bool clicked = ImGui::Button(icon, ImVec2(iconSize.x + 20, iconSize.y + 20));

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            ImGui::PopFont();

            if (!isEnabled)
                ImGui::EndDisabled();

            return clicked;
        }

        void tooltipIfHovered(const char *text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", text);
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
        renderConnectionPanel(snapshot);
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

    void UISystem::renderConnectionPanel(const SharedState::Snapshot &snapshot)
    {
        syncConnectionEditor();

        ImGui::Begin("Соединение");

        const auto status = snapshot.connectionStatus;
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
            if (renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_POWER, CommandButtonState::Idle, true, renderingSystem.getContentScale() * 68.0f))
            {
                persistConnectionEditor();
                protocolClient.connect();
            }

            tooltipIfHovered("Подключиться к дрону");
        }
        else
        {
            // Кнопка DISCONNECT (заблокирована, если в процессе соединения)
            bool canDisconnect = (status != SharedState::ConnectionStatus::connecting);
            if (renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_POWER, CommandButtonState::Idle, canDisconnect, renderingSystem.getContentScale() * 68.0f))
            {
                protocolClient.disconnect();
            }

            tooltipIfHovered(canDisconnect ? "Отключиться от дрона" : "Ожидание подключения...");
        }

        ImGui::SameLine();

        const bool lockFields = status != SharedState::ConnectionStatus::disconnected;
        if (ImGui::BeginChild("ConnectionSettings", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX))
        {
            if (lockFields) ImGui::BeginDisabled();

            ImGui::SetNextItemWidth(renderingSystem.getContentScale() * 160);
            if (ImGui::InputText("IP", ipAddressBuffer.data(), ipAddressBuffer.size()))
                persistConnectionEditor();
            tooltipIfHovered("IP-адрес дрона");

            ImGui::SetNextItemWidth(renderingSystem.getContentScale() * 160);
            if (ImGui::InputInt("TCP", &tcpPortValue)) {
                tcpPortValue = std::clamp(tcpPortValue, 1, 65535);
                persistConnectionEditor();
            }
            tooltipIfHovered("Порт TCP для команд");

            ImGui::SetNextItemWidth(renderingSystem.getContentScale() * 160);
            if (ImGui::InputInt("UDP", &udpPortValue)) {
                udpPortValue = std::clamp(udpPortValue, 1, 65535);
                persistConnectionEditor();
            }
            tooltipIfHovered("Порт UDP для телеметрии");

            if (lockFields) ImGui::EndDisabled();
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void UISystem::renderCommandsPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Команды");

        const bool isConnected =
        snapshot.connectionStatus == SharedState::ConnectionStatus::connected;
        const auto droneState = snapshot.telemetryState.currentState;
        const bool isPaused = (droneState == protocol::DroneState::PAUSED);

        const auto commandButton = [&](protocol::CommandId commandId,
                                   const char *icon,
                                   const char *tooltip,
                                   float size) -> bool
        {
            const bool commandAvailable = protocol::isCommandAvailable(
                snapshot.telemetryState.availableCommands, commandId);
            const bool enabled = isConnected && commandAvailable;
            const auto &feedback =
                snapshot.commandFeedbacks[commandFeedbackIndex(commandId)];

            CommandButtonState btnState = feedbackToButtonState(feedback);

            bool clicked = renderIconButton(
                renderingSystem.getMaterialIconsFont(), icon, btnState, enabled, renderingSystem.getContentScale() * 96.0f);

            // Tooltip
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                if (!isConnected)
                    ImGui::SetTooltip("Сначала подключись");
                else if (!commandAvailable)
                    ImGui::SetTooltip("%s — недоступно", tooltip);
                else
                    ImGui::SetTooltip("%s", tooltip);
            }

            if (clicked)
                protocolClient.sendCommand(commandId);

            return clicked;
        };

        commandButton(protocol::CommandId::PREPARE, ICON_MS_FACT_CHECK, "Подготовка дрона к полёту", renderingSystem.getContentScale() * 96.0f);

        ImGui::SameLine();

        // Комбо-кнопка Взлёт/Посадка
        {
            const bool takeoffAvailable = protocol::isCommandAvailable(
                snapshot.telemetryState.availableCommands, protocol::CommandId::TAKEOFF);
            const bool landAvailable = protocol::isCommandAvailable(
                snapshot.telemetryState.availableCommands, protocol::CommandId::LAND);

            // логика переключения
            const bool showLand = landAvailable ||
                (!takeoffAvailable && !landAvailable &&
                (droneState == protocol::DroneState::IN_FLIGHT ||
                droneState == protocol::DroneState::EXECUTING_MISSION ||
                droneState == protocol::DroneState::PAUSED ||
                droneState == protocol::DroneState::RETURNING_HOME ||
                droneState == protocol::DroneState::LANDING));

            const protocol::CommandId activeCommand =
                showLand ? protocol::CommandId::LAND : protocol::CommandId::TAKEOFF;
            const char *icon =
                showLand ? ICON_MS_FLIGHT_LAND : ICON_MS_FLIGHT_TAKEOFF;

            const bool available = protocol::isCommandAvailable(snapshot.telemetryState.availableCommands, activeCommand);
            const bool enabled = isConnected && available;

            // Feedback ТОЛЬКО от текущей активной команды
            const auto &feedback = snapshot.commandFeedbacks[commandFeedbackIndex(activeCommand)];

            CommandButtonState btnState = feedbackToButtonState(feedback);

            if (renderIconButton(renderingSystem.getMaterialIconsFont(), icon, btnState, enabled, renderingSystem.getContentScale() * 96.0f))
            {
                protocolClient.sendCommand(activeCommand);
            }

            // Tooltip
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                if (!isConnected)
                    ImGui::SetTooltip("Сначала подключитесь");
                else if (!enabled && showLand)
                    ImGui::SetTooltip("Посадка — недоступна");
                else if (!enabled)
                    ImGui::SetTooltip("Взлёт — недоступен");
                else if (showLand)
                    ImGui::SetTooltip("Посадка на текущую позицию");
                else
                    ImGui::SetTooltip("Взлёт на %.0f м", snapshot.missionParameters.payload.takeoffAltitudeM);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Миссия, Пауза, Домой
        commandButton(protocol::CommandId::START_MISSION, ICON_MS_ROUTE, "Начать выполнение миссии", renderingSystem.getContentScale() * 96.0f);

        ImGui::SameLine();
        commandButton(protocol::CommandId::PAUSE_RESUME,
                    isPaused ? ICON_MS_PLAY_CIRCLE : ICON_MS_PAUSE,
                    isPaused ? "Продолжить миссию" : "Приостановить миссию", renderingSystem.getContentScale() * 96.0f);

        ImGui::SameLine();

        commandButton(protocol::CommandId::RETURN_HOME, ICON_MS_HOME, "Возврат на точку старта", renderingSystem.getContentScale() * 96.0f);

        // Аварийная остановка
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        {
            const auto cmdId = protocol::CommandId::EMERGENCY_STOP;
            const bool commandAvailable = protocol::isCommandAvailable(snapshot.telemetryState.availableCommands, cmdId);
            const bool enabled = isConnected && commandAvailable;
            const auto &feedback = snapshot.commandFeedbacks[commandFeedbackIndex(cmdId)];

            CommandButtonState btnState = feedbackToButtonState(feedback);

            if (renderEmergencyButton(renderingSystem.getMaterialIconsFont(), ICON_MS_DESTRUCTION, btnState, enabled, renderingSystem.getContentScale() * 96.0f))
            {
                if (emergencyStopArmed)
                {
                    protocolClient.sendCommand(cmdId);
                    emergencyStopArmed = false;
                }
                else
                {
                    emergencyStopArmed = true;
                }
            }

            // Tooltip
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                if (!enabled)
                    ImGui::SetTooltip("Аварийная остановка — недоступна");
                else if (emergencyStopArmed)
                    ImGui::SetTooltip("Нажмите ещё раз для подтверждения!");
                else
                    ImGui::SetTooltip("Аварийная остановка двигателей\n(двойное нажатие)");
            }

            // Сброс если мышь ушла
            if (emergencyStopArmed && !ImGui::IsItemHovered())
                emergencyStopArmed = false;

            // Пульсирующая рамка когда взведена
            if (emergencyStopArmed && enabled)
            {
                float t = 0.5f + 0.5f * sinf(static_cast<float>(ImGui::GetTime()) * 8.0f);
                ImGui::GetWindowDrawList()->AddRect(
                    ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    ImGui::GetColorU32(ImVec4(1.0f, 0.2f + t * 0.3f, 0.1f, 0.7f + t * 0.3f)),
                    4.0f, 0, 3.0f);
            }
        }

        ImGui::End();
    }

    void UISystem::renderMissionParametersPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Параметры миссии");

        auto missionParameters = snapshot.missionParameters;

        int delayedStartSeconds = static_cast<int>(missionParameters.payload.delayedStartTimeSec);
        ImGui::SetNextItemWidth(renderingSystem.getContentScale() * 160);
        if (ImGui::InputInt("Задержка", &delayedStartSeconds))//добавить tooltip - задержка старта в секундах
        {
            delayedStartSeconds = std::max(delayedStartSeconds, 0);
            missionParameters.payload.delayedStartTimeSec = static_cast<std::uint32_t>(delayedStartSeconds);
            persistMissionParameters(missionParameters);
        }

        float takeoffAltitude = missionParameters.payload.takeoffAltitudeM;
        ImGui::SetNextItemWidth(renderingSystem.getContentScale() * 160);
        if (ImGui::InputFloat("Высота взлёта", &takeoffAltitude, 0.5f, 1.0f, "%.1f")) //добавить tooltip - m
        {
            missionParameters.payload.takeoffAltitudeM = std::clamp(takeoffAltitude, 1.0f, 100.0f);
            persistMissionParameters(missionParameters);
        }

        float flightSpeed = missionParameters.payload.flightSpeedMS;
        ImGui::SetNextItemWidth(renderingSystem.getContentScale() * 160);
        if (ImGui::InputFloat("Скорость", &flightSpeed, 0.1f, 0.5f, "%.1f")) //добавить tooltip  - скорость полёта m/s
        {
            missionParameters.payload.flightSpeedMS = std::clamp(flightSpeed, 0.5f, 20.0f);
            persistMissionParameters(missionParameters);
        }

        int flightModeIndex = missionParameters.flightMode == protocol::FlightMode::AUTOMATIC ? 0 : 1;
        const char *flightModes[] = {"Авто", "Полуавто"};
        ImGui::SetNextItemWidth(renderingSystem.getContentScale() * 160);
        if (ImGui::Combo("Режим", &flightModeIndex, flightModes, IM_ARRAYSIZE(flightModes)))
        {
            missionParameters.flightMode = flightModeIndex == 0 ? protocol::FlightMode::AUTOMATIC : protocol::FlightMode::SEMI_AUTOMATIC;
            persistMissionParameters(missionParameters);
            protocolClient.sendMode(missionParameters.flightMode);
        }

        if (renderIconButton(renderingSystem.getMaterialIconsFont(),  ICON_MS_SEND, CommandButtonState::Idle, true, renderingSystem.getContentScale() * 52.0f))
        {
            protocolClient.sendMissionParameters(missionParameters);
        }

        ImGui::SameLine();

        if (renderIconButton(renderingSystem.getMaterialIconsFont(),  ICON_MS_SAVE, CommandButtonState::Idle, true, renderingSystem.getContentScale() * 52.0f))//tooltip - сохранить в файл
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

        if (renderIconButton(renderingSystem.getMaterialIconsFont(),  ICON_MS_UPLOAD_FILE, CommandButtonState::Idle, true, renderingSystem.getContentScale() * 52.0f))//tooltip - загрузка из файла
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
        ImGui::Begin("Телеметрия");

        const auto droneState = snapshot.telemetryState.currentState;
        const ImVec4 indicatorColor = stateColor(droneState);

        renderStatusIndicator(protocol::droneStateToString(droneState), indicatorColor, renderingSystem.getBoldFont());

        ImGui::Text("Mode: %s", protocol::flightModeToString(snapshot.telemetryState.flightMode));
        ImGui::Separator();

        if  (ImGui::BeginChild("#Position", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX))
        {
            ImGui::Text("X: %.2f m", snapshot.telemetryPosition.posX);
            ImGui::Text("Y: %.2f m", snapshot.telemetryPosition.posY);
            ImGui::Text("Z: %.2f m", snapshot.telemetryPosition.posZ);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("#Velocity", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX))
        {
            ImGui::Text("V_X: %.2f m/s", snapshot.telemetryPosition.velX);
            ImGui::Text("V_Y: %.2f m/s", snapshot.telemetryPosition.velY);
            ImGui::Text("V_Z: %.2f m/s", snapshot.telemetryPosition.velZ);
        }
        ImGui::EndChild();

        
        ImGui::Separator();
        ImGui::Text("Высота AGL: %.2f m", snapshot.telemetryPosition.altitudeAglM);
        ImGui::Text("Курс: %.1f deg", snapshot.telemetryPosition.headingDeg);

        ImGui::Separator();
        ImGui::Text("Батарея");
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, batteryColor(snapshot.telemetryState.batteryPercent));
        ImGui::ProgressBar(snapshot.telemetryState.batteryPercent / 100.0f,
                        ImVec2(-1.0f, 0.0f),
                        (std::to_string(snapshot.telemetryState.batteryPercent) + "%").c_str());
        ImGui::PopStyleColor();

        ImGui::End();
    }

    void UISystem::renderSimulationPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Лидар");

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

        const auto verticalState = simulation.obstacles.vertical;
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
                        simulation.obstacles.vertical = protocol::VerticalObstacle::ABOVE;
                    }
                    else if (verticalState == protocol::VerticalObstacle::ABOVE)
                    {
                        simulation.obstacles.vertical = protocol::VerticalObstacle::BELOW;
                    }
                    else
                    {
                        simulation.obstacles.vertical = protocol::VerticalObstacle::NONE;
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
        if (ImGui::Checkbox("LiDAR", &lidarActive))
        {
            simulation.lidar.lidarActive = lidarActive;
            sharedState.setSimulationModel(simulation);
            protocolClient.sendSimulationLidar(lidarActive);
        }

        ImGui::End();
    }

    void UISystem::renderLogPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Журнал");

        ImGui::Checkbox("Команды", &showCommandLogs);
        ImGui::SameLine();
        ImGui::Checkbox("Телеметрия", &showTelemetryLogs);
        ImGui::SameLine();
        ImGui::Checkbox("Ошибки", &showErrorLogs);

        auto preferences = snapshot.uiPreferences;
        ImGui::SameLine();
        if (ImGui::Checkbox("Автопрокрутка", &preferences.autoScrollLog))
        {
            sharedState.setUiPreferences(preferences);
        }

        ImGui::SameLine();
        if (renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_DELETE, CommandButtonState::Idle, true, 0.0f))
        {
            sharedState.clearLogs();
        }
        tooltipIfHovered("Очистить журнал");

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
        ImGui::Begin("Облако точек", nullptr, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

        float customSpacing = renderingSystem.getContentScale() * 100.0f;

        auto preferences = snapshot.uiPreferences;
        ImGui::SetNextItemWidth(renderingSystem.getContentScale() * 160);
        if (ImGui::SliderFloat("Point size", &preferences.pointSize, 1.0f, 8.0f, "%.1f"))
        {
            sharedState.setUiPreferences(preferences);
        }

        ImGui::SameLine(0.0f, customSpacing);
        int colorModeIndex = preferences.colorMode == SharedState::ColorMode::intensity ? 0 : 1;
        const char *colorModes[] = {"Интенсивность", "Расстояние"};
        ImGui::SetNextItemWidth(renderingSystem.getContentScale() * 160);
        if (ImGui::Combo("Режим цвета", &colorModeIndex, colorModes, IM_ARRAYSIZE(colorModes)))
        {
            preferences.colorMode = colorModeIndex == 0 ? SharedState::ColorMode::intensity : SharedState::ColorMode::distance;
            sharedState.setUiPreferences(preferences);
        }

        ImGui::SameLine(0.0f, customSpacing);

        ImGui::SetNextItemWidth(renderingSystem.getContentScale() * 160);
        if (ImGui::Checkbox("Размер по расстоянию", &preferences.distancePointSizing))
        {
            sharedState.setUiPreferences(preferences);
        }

        ImGui::SameLine(0.0f, customSpacing);
        if (renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_RESTART_ALT, CommandButtonState::Idle, true, ImGui::GetTextLineHeight()))
        {
            pointCloudRenderer.getCamera().reset();
        }
        tooltipIfHovered("Сбросить камеру в начальное положение");

        ImGui::SameLine(0.0f, customSpacing);

        ImGui::Text("Кадры: %u, точки: %zu",
                    snapshot.pointCloud.timestampMs,
                    snapshot.pointCloud.points.size());
        ImGui::Separator();

        ImVec2 viewerSize = ImGui::GetContentRegionAvail();
        viewerSize.x = std::max(viewerSize.x, 200.0f);
        viewerSize.y = std::max(viewerSize.y, 240.0f);

        viewer::SceneOverlay overlay;

        // Вектор скорости дрона
        overlay.arrows.push_back({
            .origin    = glm::vec3(0.0f, 0.0f, 0.0f), // Позиция дрона
            .direction = glm::vec3(snapshot.telemetryPosition.velX, snapshot.telemetryPosition.velY, snapshot.telemetryPosition.velZ) * 2.0f, // Масштабированная скорость
            .color     = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
            .thickness = 3.0f,
            .headSize  = 0.25f,
        });

        // Вектор курса
        float headRad = glm::radians(snapshot.telemetryPosition.headingDeg);
        overlay.arrows.push_back({
            .origin    = glm::vec3(0.0f, 0.0f, 0.0f),
            .direction = glm::vec3(std::sin(headRad), 0.0f, std::cos(headRad)) * 3.0f,
            .color     = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f),
            .thickness = 2.0f,
        });

        // AABB-аппроксимация кластера точек
        overlay.boxes.push_back({
            .center      = glm::vec3(5.0f, 2.0f, 3.0f),
            .halfExtents = glm::vec3(1.5f, 1.0f, 2.0f),
            .faceColor   = glm::vec4(1.0f, 0.3f, 0.3f, 0.15f),
            .edgeColor   = glm::vec4(1.0f, 0.4f, 0.4f, 0.9f), 
            .edgeThickness = 2.0f,
        });

        // Зона обнаружения препятствия
        overlay.boxes.push_back({
            .center      = glm::vec3(-3.0f, 1.5f, 0.0f),
            .halfExtents = glm::vec3(2.0f, 2.0f, 2.0f),
            .faceColor   = glm::vec4(0.2f, 0.8f, 0.2f, 0.10f),
            .edgeColor   = glm::vec4(0.3f, 0.9f, 0.3f, 0.7f),
            .edgeThickness = 1.5f,
        });

        // Линия между двумя точками
        overlay.lines.push_back({
            .start     = glm::vec3(0.0f, 0.0f, 0.0f),
            .end       = glm::vec3(5.0f, 2.0f, 3.0f),
            .color     = glm::vec4(0.5f, 0.5f, 1.0f, 0.6f),
            .thickness = 1.0f,
        });

        pointCloudRenderer.render(snapshot.pointCloud, preferences, viewerSize, overlay);
        ImGui::Image(pointCloudRenderer.getTextureRef(), viewerSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

        const bool hovered = ImGui::IsItemHovered();
        pointCloudRenderer.getCamera().handleInput(hovered, viewerSize);

        const ImVec2 imageMin = ImGui::GetItemRectMin();
        pointCloudRenderer.drawAxisOverlay(ImGui::GetWindowDrawList(), ImVec2(imageMin.x + 32.0f, imageMin.y + 36.0f), 20.0f);

        ImGui::TextDisabled("ЛКМ: вращение  |  СКМ: перемещение  |  Колесо: зум");
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

}
