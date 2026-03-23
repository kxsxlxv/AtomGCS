#pragma once

#include "config/AppConfig.h"
#include "core/PathUtils.h"
#include "network/ProtocolClient.h"
#include "rendering/RenderingSystem.h"
#include "state/SharedState.h"
#include "viewer/PointCloudRenderer.h"

#include <array>
#include <string>

namespace gcs
{
    enum class CommandButtonState
    {
        Idle,       // Нейтральное — ничего не происходит
        Pending    // Команда отправлена, ожидание ответа
    };

    // ImGui панели
    class UISystem
    {
    public:
        UISystem(RenderingSystem &renderingSystem,
                SharedState &sharedState,
                network::ProtocolClient &protocolClient,
                viewer::PointCloudRenderer &pointCloudRenderer,
                const ApplicationPaths &applicationPaths);

        void render();

    private:
        void renderDockspace();

        void renderConnectionPanel(const SharedState::Snapshot &snapshot);
        void renderCommandsPanel(const SharedState::Snapshot &snapshot);
        void renderMissionParametersPanel(const SharedState::Snapshot &snapshot);
        void renderTelemetryPanel(const SharedState::Snapshot &snapshot) const;
        void renderSimulationPanel(const SharedState::Snapshot &snapshot);
        void renderLogPanel(const SharedState::Snapshot &snapshot);
        void renderPointCloudPanel(const SharedState::Snapshot &snapshot);

        void syncConnectionEditor();
        void persistConnectionEditor() const;
        void persistMissionParameters(const SharedState::MissionParametersModel &missionParameters) const;

        static ImVec4 commandButtonColor(const SharedState::CommandFeedback &feedback);
        static bool isCommandFeedbackVisible(const SharedState::CommandFeedback &feedback);

        RenderingSystem &renderingSystem;
        SharedState &sharedState;
        network::ProtocolClient &protocolClient;
        viewer::PointCloudRenderer &pointCloudRenderer;
        ApplicationPaths applicationPaths;

        std::array<char, 64> ipAddressBuffer{};
        int tcpPortValue = 5760;
        int udpPortValue = 5761;
        bool connectionEditorInitialized = false;

        bool showCommandLogs = true;
        bool showTelemetryLogs = true;
        bool showErrorLogs = true;

        bool emergencyStopArmed = false;
    };

}