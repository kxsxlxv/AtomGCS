#include "ui/UISystem.h"
#include "ui/UiHelpers.h"

#include "shared/protocol/protocol_utils.h"

#include <fonts/IconsMaterialSymbols.h>

#include <imgui.h>

#include <cmath>

namespace gcs
{

    void UISystem::renderCommandsPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Команды");

        const bool isConnected = snapshot.connectionStatus == SharedState::ConnectionStatus::connected;
        const auto droneState = snapshot.telemetryState.currentState;
        const bool isPaused = (droneState == protocol::DroneState::PAUSED);

        const float commandButtonSize = ImGui::GetFontSize() * 4.0f;

        const auto commandButton = [&](protocol::CommandId commandId,
                                   const char *icon,
                                   const char *tooltip,
                                   float size) -> bool
        {
            const bool commandAvailable = protocol::isCommandAvailable(snapshot.telemetryState.availableCommands, commandId);
            const bool enabled = isConnected && commandAvailable;
            const auto &feedback = snapshot.commandFeedbacks[ui::commandFeedbackIndex(commandId)];

            CommandButtonState btnState = ui::feedbackToButtonState(feedback);

            bool clicked = ui::renderIconButton(renderingSystem.getMaterialIconsFont(), icon, btnState, enabled, size);

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

        commandButton(protocol::CommandId::PREPARE, ICON_MS_FACT_CHECK, "Подготовка дрона к полёту", commandButtonSize);

        ImGui::SameLine();

        {
            const bool takeoffAvailable = protocol::isCommandAvailable(snapshot.telemetryState.availableCommands, protocol::CommandId::TAKEOFF);
            const bool landAvailable = protocol::isCommandAvailable(snapshot.telemetryState.availableCommands, protocol::CommandId::LAND);

            const bool showLand = landAvailable ||
                (!takeoffAvailable && !landAvailable &&
                (droneState == protocol::DroneState::IN_FLIGHT ||
                droneState == protocol::DroneState::EXECUTING_MISSION ||
                droneState == protocol::DroneState::PAUSED ||
                droneState == protocol::DroneState::RETURNING_HOME ||
                droneState == protocol::DroneState::LANDING));

            const protocol::CommandId activeCommand = showLand ? protocol::CommandId::LAND : protocol::CommandId::TAKEOFF;
            const char *icon = showLand ? ICON_MS_FLIGHT_LAND : ICON_MS_FLIGHT_TAKEOFF;

            const bool available = protocol::isCommandAvailable(snapshot.telemetryState.availableCommands, activeCommand);
            const bool enabled = isConnected && available;

            const auto &feedback = snapshot.commandFeedbacks[ui::commandFeedbackIndex(activeCommand)];

            CommandButtonState btnState = ui::feedbackToButtonState(feedback);

            if (ui::renderIconButton(renderingSystem.getMaterialIconsFont(), icon, btnState, enabled, commandButtonSize))
            {
                protocolClient.sendCommand(activeCommand);
            }

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

        commandButton(protocol::CommandId::START_MISSION, ICON_MS_ROUTE, "Начать выполнение миссии", commandButtonSize);

        ImGui::SameLine();
        commandButton(protocol::CommandId::PAUSE_RESUME,
                    isPaused ? ICON_MS_PLAY_CIRCLE : ICON_MS_PAUSE,
                    isPaused ? "Продолжить миссию" : "Приостановить миссию", commandButtonSize);

        ImGui::SameLine();

        commandButton(protocol::CommandId::RETURN_HOME, ICON_MS_HOME, "Возврат на точку старта", commandButtonSize);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        {
            const auto cmdId = protocol::CommandId::EMERGENCY_STOP;
            const bool commandAvailable = protocol::isCommandAvailable(snapshot.telemetryState.availableCommands, cmdId);
            const bool enabled = isConnected && commandAvailable;
            const auto &feedback = snapshot.commandFeedbacks[ui::commandFeedbackIndex(cmdId)];

            CommandButtonState btnState = ui::feedbackToButtonState(feedback);

            if (ui::renderEmergencyButton(renderingSystem.getMaterialIconsFont(), ICON_MS_DESTRUCTION, btnState, enabled, commandButtonSize))
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

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                if (!enabled)
                    ImGui::SetTooltip("Аварийная остановка — недоступна");
                else if (emergencyStopArmed)
                    ImGui::SetTooltip("Нажмите ещё раз для подтверждения!");
                else
                    ImGui::SetTooltip("Аварийная остановка двигателей\n(двойное нажатие)");
            }

            if (emergencyStopArmed && !ImGui::IsItemHovered())
                emergencyStopArmed = false;

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

} // namespace gcs