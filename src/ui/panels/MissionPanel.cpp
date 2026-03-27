#include "ui/UISystem.h"
#include "ui/UiHelpers.h"

#include "config/AppConfig.h"

#include <fonts/IconsMaterialSymbols.h>

#include <imgui.h>

#include <algorithm>

namespace gcs
{

    void UISystem::renderMissionParametersPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Миссия");

        auto missionParameters = snapshot.missionParameters;

        const float missionInputWidth = ImGui::GetFontSize() * 8.0f;
        const float missionIconButtonSize = ImGui::GetFontSize() * 2.0f;

        int delayedStartSeconds = static_cast<int>(missionParameters.payload.delayedStartTimeSec);
        ImGui::SetNextItemWidth(missionInputWidth);
        if (ImGui::InputInt("Задержка", &delayedStartSeconds))
        {
            delayedStartSeconds = std::max(delayedStartSeconds, 0);
            missionParameters.payload.delayedStartTimeSec = static_cast<std::uint32_t>(delayedStartSeconds);
            persistMissionParameters(missionParameters);
        }
        ui::tooltipIfHovered("Задержка старта в секундах");

        float takeoffAltitude = missionParameters.payload.takeoffAltitudeM;
        ImGui::SetNextItemWidth(missionInputWidth);
        if (ImGui::InputFloat("Высота взлёта", &takeoffAltitude, 0.5f, 1.0f, "%.1f"))
        {
            missionParameters.payload.takeoffAltitudeM = std::clamp(takeoffAltitude, 1.0f, 100.0f);
            persistMissionParameters(missionParameters);
        }
        ui::tooltipIfHovered("Высота взлёта в метрах");

        float flightSpeed = missionParameters.payload.flightSpeedMS;
        ImGui::SetNextItemWidth(missionInputWidth);
        if (ImGui::InputFloat("Скорость", &flightSpeed, 0.1f, 0.5f, "%.1f"))
        {
            missionParameters.payload.flightSpeedMS = std::clamp(flightSpeed, 0.5f, 20.0f);
            persistMissionParameters(missionParameters);
        }
        ui::tooltipIfHovered("Скорость полёта, м/с");

        int flightModeIndex = missionParameters.flightMode == protocol::FlightMode::AUTOMATIC ? 0 : 1;
        const char *flightModes[] = {"Авто", "Полуавто"};
        ImGui::SetNextItemWidth(missionInputWidth);
        if (ImGui::Combo("Режим", &flightModeIndex, flightModes, IM_ARRAYSIZE(flightModes)))
        {
            missionParameters.flightMode = flightModeIndex == 0 ? protocol::FlightMode::AUTOMATIC : protocol::FlightMode::SEMI_AUTOMATIC;
            persistMissionParameters(missionParameters);
            protocolClient.sendMode(missionParameters.flightMode);
        }

        if (ui::renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_SEND, CommandButtonState::Idle, true, missionIconButtonSize))
        {
            protocolClient.sendMissionParameters(missionParameters);
        }
        ui::tooltipIfHovered("Отправить параметры дрону");

        ImGui::SameLine();

        if (ui::renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_SAVE, CommandButtonState::Idle, true, missionIconButtonSize))
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
        ui::tooltipIfHovered("Сохранить в файл");

        ImGui::SameLine();

        if (ui::renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_UPLOAD_FILE, CommandButtonState::Idle, true, missionIconButtonSize))
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
        ui::tooltipIfHovered("Загрузить из файла");

        ImGui::End();
    }

} // namespace gcs