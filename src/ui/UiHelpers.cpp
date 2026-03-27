#include "ui/UiHelpers.h"

#include "shared/protocol/protocol_utils.h"

#include <fonts/IconsMaterialSymbols.h>

#include <imgui.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

namespace gcs
{

    namespace ui
    {

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

        std::size_t commandFeedbackIndex(protocol::CommandId commandId)
        {
            return static_cast<std::size_t>(static_cast<std::uint8_t>(commandId) - 1U);
        }

        CommandButtonState feedbackToButtonState(const SharedState::CommandFeedback &feedback)
        {
            if (feedback.pending)
                return CommandButtonState::Pending;

            return CommandButtonState::Idle;
        }

        void renderStatusIndicator(const char *label, ImVec4 color, ImFont *font)
        {
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            ImVec2 screenPosition = ImGui::GetCursorScreenPos();
            float textLineHeight = ImGui::GetTextLineHeight();

            const float circleRadius = ImGui::GetFontSize() * 0.25f;
            const float paddingX = ImGui::GetStyle().ItemInnerSpacing.x;

            ImU32 uColor = ImGui::GetColorU32(color);
            ImVec2 circleCenter = ImVec2(screenPosition.x + circleRadius, screenPosition.y + textLineHeight * 0.5f);
            drawList->AddCircleFilled(circleCenter, circleRadius, uColor);

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (circleRadius * 2.0f) + paddingX);

            if (font) ImGui::PushFont(font);
            ImGui::TextColored(color, "%s", label);
            if (font) ImGui::PopFont();
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

        void renderIconLabel(ImFont *iconFont, const char *icon, const char *labelAfter)
        {
            const float iconSize = ImGui::GetFontSize();
            ImGui::PushFont(iconFont, iconSize);
            ImGui::Text("%s", icon);
            ImGui::PopFont();
            if (labelAfter && labelAfter[0])
            {
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::Text("%s", labelAfter);
            }
        }

        void renderIconLabelColored(ImFont *iconFont, const char *icon, ImVec4 color, const char *labelAfter)
        {
            const float iconSize = ImGui::GetFontSize();
            ImGui::PushFont(iconFont, iconSize);
            ImGui::TextColored(color, "%s", icon);
            ImGui::PopFont();
            if (labelAfter && labelAfter[0])
            {
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::TextColored(color, "%s", labelAfter);
            }
        }

        void renderIconLabelDisabled(ImFont *iconFont, const char *icon, const char *labelAfter)
        {
            const float iconSize = ImGui::GetFontSize();
            ImGui::PushFont(iconFont, iconSize);
            ImGui::TextDisabled("%s", icon);
            ImGui::PopFont();
            if (labelAfter && labelAfter[0])
            {
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::TextDisabled("%s", labelAfter);
            }
        }

    } // namespace ui

} // namespace gcs