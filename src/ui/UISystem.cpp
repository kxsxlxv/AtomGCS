#include "ui/UISystem.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace gcs
{

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