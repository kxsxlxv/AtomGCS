#include "ui/UISystem.h"
#include "ui/UiHelpers.h"

#include <fonts/IconsMaterialSymbols.h>

#include <imgui.h>

#include <algorithm>

namespace gcs
{

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

        ui::renderStatusIndicator(toString(status), statusColor, renderingSystem.getBoldFont());

        ImGui::Spacing();

        const float frameHeight = ImGui::GetFrameHeight();
        const float connectButtonHeight = frameHeight * 2.95f;

        if (status == SharedState::ConnectionStatus::disconnected)
        {
            if (ui::renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_POWER, CommandButtonState::Idle, true, connectButtonHeight))
            {
                persistConnectionEditor();
                protocolClient.connect();
            }
            ui::tooltipIfHovered("Подключиться к дрону");
        }
        else
        {
            bool canDisconnect = (status != SharedState::ConnectionStatus::connecting);
            if (ui::renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_POWER, CommandButtonState::Idle, canDisconnect, connectButtonHeight))
            {
                protocolClient.disconnect();
            }
            ui::tooltipIfHovered(canDisconnect ? "Отключиться от дрона" : "Ожидание подключения...");
        }

        ImGui::SameLine();

        const float inputWidth = ImGui::GetFontSize() * 7.0f;

        const bool lockFields = status != SharedState::ConnectionStatus::disconnected;
        if (ImGui::BeginChild("ConnectionSettings", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX))
        {
            if (lockFields) ImGui::BeginDisabled();

            ImGui::SetNextItemWidth(inputWidth);
            if (ImGui::InputText("IP", ipAddressBuffer.data(), ipAddressBuffer.size()))
                persistConnectionEditor();
            ui::tooltipIfHovered("IP-адрес дрона");

            ImGui::SetNextItemWidth(inputWidth);
            if (ImGui::InputInt("TCP", &tcpPortValue)) {
                tcpPortValue = std::clamp(tcpPortValue, 1, 65535);
                persistConnectionEditor();
            }
            ui::tooltipIfHovered("Порт TCP для команд");

            ImGui::SetNextItemWidth(inputWidth);
            if (ImGui::InputInt("UDP", &udpPortValue)) {
                udpPortValue = std::clamp(udpPortValue, 1, 65535);
                persistConnectionEditor();
            }
            ui::tooltipIfHovered("Порт UDP для телеметрии");

            if (lockFields) ImGui::EndDisabled();
        }
        ImGui::EndChild();

        ImGui::End();
    }

} // namespace gcs