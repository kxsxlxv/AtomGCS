#include "ui/UISystem.h"
#include "ui/UiHelpers.h"

#include "shared/protocol/protocol_utils.h"

#include <fonts/IconsMaterialSymbols.h>

#include <imgui.h>

#include <string>

namespace gcs
{

    void UISystem::renderTelemetryPanel(const SharedState::Snapshot &snapshot) const
    {
        ImGui::Begin("Телеметрия");

        const auto droneState = snapshot.telemetryState.currentState;
        const ImVec4 indicatorColor = ui::stateColor(droneState);

        ui::renderStatusIndicator(protocol::droneStateToString(droneState), indicatorColor, renderingSystem.getBoldFont());

        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        ui::renderIconLabelDisabled(renderingSystem.getMaterialIconsFont(), ICON_MS_NAVIGATION, protocol::flightModeToString(snapshot.telemetryState.flightMode));

        ImGui::Separator();

        ui::renderIconLabelColored(renderingSystem.getMaterialIconsFont(), ICON_MS_EXPLORE, ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "Позиция (м)");
        ImGui::Text("  X: %.2f   Y: %.2f   Z: %.2f",
                    snapshot.telemetryPosition.posX,
                    snapshot.telemetryPosition.posY,
                    snapshot.telemetryPosition.posZ);

        ImGui::Spacing();

        ui::renderIconLabelColored(renderingSystem.getMaterialIconsFont(), ICON_MS_SPEED, ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "Скорость (м/с)");
        ImGui::Text("  X: %.2f   Y: %.2f   Z: %.2f",
                    snapshot.telemetryPosition.velX,
                    snapshot.telemetryPosition.velY,
                    snapshot.telemetryPosition.velZ);

        ImGui::Separator();

        ui::renderIconLabel(renderingSystem.getMaterialIconsFont(), ICON_MS_ALTITUDE, nullptr);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::Text("Высота AGL: %.2f м", snapshot.telemetryPosition.altitudeAglM);

        ui::renderIconLabel(renderingSystem.getMaterialIconsFont(), ICON_MS_EXPLORE, nullptr);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::Text("Курс: %.1f °", snapshot.telemetryPosition.headingDeg);

        ImGui::Separator();

        const auto batteryPercent = snapshot.telemetryState.batteryPercent;
        const char *batteryIcon = ICON_MS_BATTERY_FULL;
        if (batteryPercent <= 20)
            batteryIcon = ICON_MS_BATTERY_ALERT;
        else if (batteryPercent <= 50)
            batteryIcon = ICON_MS_BATTERY_5_BAR;

        ui::renderIconLabel(renderingSystem.getMaterialIconsFont(), batteryIcon, "Батарея");
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ui::batteryColor(batteryPercent));
        ImGui::ProgressBar(batteryPercent / 100.0f,
                        ImVec2(-1.0f, 0.0f),
                        (std::to_string(batteryPercent) + "%").c_str());
        ImGui::PopStyleColor();

        ImGui::End();
    }

} // namespace gcs