#include "ui/UISystem.h"
#include "ui/UiHelpers.h"

#include <fonts/IconsMaterialSymbols.h>
#include "rendering/RenderingSystem.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include <glm/trigonometric.hpp>
#include "viewer/SceneOverlay.h"
#include "viewer/CubeGizmo.h"

namespace gcs
{

    void UISystem::renderPointCloudPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Облако точек", nullptr, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

        const float controlSpacing = ImGui::GetFontSize() * 3.8f;
        const float controlWidth = ImGui::GetFontSize() * 8.0f;

        auto preferences = snapshot.uiPreferences;
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::SliderFloat("Размер точки", &preferences.pointSize, 1.0f, 8.0f, "%.1f"))
        {
            sharedState.setUiPreferences(preferences);
        }

        ImGui::SameLine(0.0f, controlSpacing);
        int colorModeIndex = preferences.colorMode == SharedState::ColorMode::intensity ? 0 : 1;
        const char *colorModes[] = {"Интенсивность", "Расстояние"};
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Режим цвета", &colorModeIndex, colorModes, IM_ARRAYSIZE(colorModes)))
        {
            preferences.colorMode = colorModeIndex == 0 ? SharedState::ColorMode::intensity : SharedState::ColorMode::distance;
            sharedState.setUiPreferences(preferences);
        }

        ImGui::SameLine(0.0f, controlSpacing);
        if (ImGui::Checkbox("Размер по расстоянию", &preferences.distancePointSizing))
        {
            sharedState.setUiPreferences(preferences);
        }

        ImGui::SameLine(0.0f, controlSpacing);
        if (ui::renderIconButton(renderingSystem.getMaterialIconsFont(), ICON_MS_RESTART_ALT, CommandButtonState::Idle, true, ImGui::GetTextLineHeight()))
        {
            pointCloudRenderer.getCamera().reset();
        }
        ui::tooltipIfHovered("Сбросить камеру в начальное положение");

        ImGui::SameLine(0.0f, controlSpacing);
        ImGui::PushFont(renderingSystem.getMaterialIconsFont(), ImGui::GetFontSize());
        ImGui::TextDisabled("%s", ICON_MS_3D_ROTATION);
        ImGui::PopFont();
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x * 0.5f);
        ImGui::TextDisabled("%u кадр  |  %zu точек",
                    snapshot.pointCloud.timestampMs,
                    snapshot.pointCloud.points.size());
        ImGui::Separator();

        ImVec2 viewerSize = ImGui::GetContentRegionAvail();
        viewerSize.x = std::max(viewerSize.x, 200.0f);
        viewerSize.y = std::max(viewerSize.y, 240.0f);

        viewer::SceneOverlay overlay;

        overlay.arrows.push_back({
            .origin    = glm::vec3(0.0f, 0.0f, 0.0f),
            .direction = glm::vec3(snapshot.telemetryPosition.velX, snapshot.telemetryPosition.velY, snapshot.telemetryPosition.velZ) * 2.0f,
            .color     = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
            .thickness = 3.0f,
            .headSize  = 0.25f,
        });

        float headRad = glm::radians(snapshot.telemetryPosition.headingDeg);
        overlay.arrows.push_back({
            .origin    = glm::vec3(0.0f, 0.0f, 0.0f),
            .direction = glm::vec3(std::sin(headRad), 0.0f, std::cos(headRad)) * 3.0f,
            .color     = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f),
            .thickness = 2.0f,
        });

        overlay.boxes.push_back({
            .center      = glm::vec3(5.0f, 2.0f, 3.0f),
            .halfExtents = glm::vec3(1.5f, 1.0f, 2.0f),
            .faceColor   = glm::vec4(1.0f, 0.3f, 0.3f, 0.15f),
            .edgeColor   = glm::vec4(1.0f, 0.4f, 0.4f, 0.9f),
            .edgeThickness = 2.0f,
        });

        overlay.boxes.push_back({
            .center      = glm::vec3(-3.0f, 1.5f, 0.0f),
            .halfExtents = glm::vec3(2.0f, 2.0f, 2.0f),
            .faceColor   = glm::vec4(0.2f, 0.8f, 0.2f, 0.10f),
            .edgeColor   = glm::vec4(0.3f, 0.9f, 0.3f, 0.7f),
            .edgeThickness = 1.5f,
        });

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
        const ImVec2 imageMax(imageMin.x + viewerSize.x, imageMin.y + viewerSize.y);

        // Куб ориентации камеры
        viewer::CubeGizmo cubeGizmo;
        cubeGizmo.render(ImGui::GetWindowDrawList(), imageMin, imageMax,
                        pointCloudRenderer.getCamera(), pointCloudRenderer.getCamera());

        // Подсказка управления — поверх изображения внизу слева
        {
            const float currentFontSize = ImGui::GetFontSize();
            ImFont *iconFont = renderingSystem.getMaterialIconsFont();
            ImFont *textFont = ImGui::GetFont();
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const ImU32 hintColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const float paddingX = currentFontSize * 0.6f;
            const float paddingY = currentFontSize * 0.4f;

            const char *hintText = "ЛКМ: вращение  |  СКМ: перемещение  |  Колесо: зум";
            const ImVec2 textSize = textFont->CalcTextSizeA(currentFontSize, FLT_MAX, 0.0f, hintText);
            const char *mouseIcon = ICON_MS_MOUSE;
            const ImVec2 iconSize = iconFont->CalcTextSizeA(currentFontSize, FLT_MAX, 0.0f, mouseIcon);

            const float totalWidth = iconSize.x + ImGui::GetStyle().ItemInnerSpacing.x * 0.5f + textSize.x;
            const float baselineY = imageMax.y - paddingY - currentFontSize;
            const float startX = imageMax.x - paddingX - totalWidth;

            drawList->AddText(iconFont, currentFontSize, ImVec2(startX, baselineY), hintColor, mouseIcon);
            const float textStartX = startX + iconSize.x + ImGui::GetStyle().ItemInnerSpacing.x * 0.5f;
            drawList->AddText(textFont, currentFontSize, ImVec2(textStartX, baselineY), hintColor, hintText);
        }

        ImGui::End();
    }

} // namespace gcs