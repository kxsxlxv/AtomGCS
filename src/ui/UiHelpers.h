#pragma once

#include "state/SharedState.h"
#include "ui/UISystem.h"

#include <imgui.h>

#include <chrono>
#include <cstdint>
#include <string>

struct ImFont;

namespace gcs
{

    namespace ui
    {

        ImVec4 stateColor(protocol::DroneState state);
        ImVec4 batteryColor(std::uint8_t batteryPercent);

        std::string formatTimestamp(const std::chrono::system_clock::time_point &timePoint);
        const char *directionLabel(SharedState::LogDirection direction);
        bool shouldShowLog(const SharedState::LogEntry &entry, bool showCommandLogs, bool showTelemetryLogs, bool showErrorLogs);

        std::size_t commandFeedbackIndex(protocol::CommandId commandId);
        CommandButtonState feedbackToButtonState(const SharedState::CommandFeedback &feedback);

        void renderStatusIndicator(const char *label, ImVec4 color, ImFont *font);
        bool renderIconButton(ImFont *font, const char *icon, CommandButtonState state, bool isEnabled, float size);
        bool renderEmergencyButton(ImFont *font, const char *icon, CommandButtonState state, bool isEnabled, float size);
        void tooltipIfHovered(const char *text);

        void renderIconLabel(ImFont *iconFont, const char *icon, const char *labelAfter);
        void renderIconLabelColored(ImFont *iconFont, const char *icon, ImVec4 color, const char *labelAfter);
        void renderIconLabelDisabled(ImFont *iconFont, const char *icon, const char *labelAfter);

    } // namespace ui

} // namespace gcs