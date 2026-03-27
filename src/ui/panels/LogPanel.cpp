#include "ui/UISystem.h"
#include "ui/UiHelpers.h"

#include <fonts/IconsMaterialSymbols.h>

#include <imgui.h>

#include <sstream>
#include <string>

namespace gcs
{

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
        if (ui::renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_DELETE_SWEEP, CommandButtonState::Idle, true, ImGui::GetTextLineHeight()))
        {
            sharedState.clearLogs();
        }
        ui::tooltipIfHovered("Очистить журнал");

        ImGui::Separator();
        ImGui::BeginChild("LogEntries");
        for (const auto &entry : snapshot.logs)
        {
            if (!ui::shouldShowLog(entry, showCommandLogs, showTelemetryLogs, showErrorLogs))
            {
                continue;
            }

            ImVec4 lineColor;
            switch (entry.category)
            {
            case SharedState::LogCategory::command:
                lineColor = ImVec4(0.290f, 0.565f, 0.851f, 1.0f);
                break;
            case SharedState::LogCategory::error:
                lineColor = ImVec4(0.937f, 0.325f, 0.314f, 1.0f);
                break;
            case SharedState::LogCategory::telemetry:
            default:
                lineColor = ImVec4(0.620f, 0.620f, 0.620f, 1.0f);
                break;
            }

            std::ostringstream line;
            line << '[' << ui::formatTimestamp(entry.timestamp) << "] [" << ui::directionLabel(entry.direction) << "] [" << entry.type << "] " << entry.description;
            ImGui::TextColored(lineColor, "%s", line.str().c_str());
        }

        if (preferences.autoScrollLog)
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }

} // namespace gcs