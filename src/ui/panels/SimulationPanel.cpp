#include "ui/UISystem.h"

#include <imgui.h>

namespace gcs
{

    void UISystem::renderSimulationPanel(const SharedState::Snapshot &snapshot)
    {
        ImGui::Begin("Лидар");

        auto simulation = snapshot.simulation;
        bool obstaclesChanged = false;

        const float cellSize = ImGui::GetFontSize() * 2.1f;
        const float cellSpacing = ImGui::GetFontSize() * 0.25f;
        const ImVec2 gridSize(cellSize * 3.0f + cellSpacing * 2.0f, cellSize * 3.0f + cellSpacing * 2.0f);
        const ImVec2 gridOrigin = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton("ObstacleGrid", gridSize);
        ImDrawList *drawList = ImGui::GetWindowDrawList();

        const auto drawCell = [&](int row, int column, ImU32 fillColor, const char *label) {
            const ImVec2 min(gridOrigin.x + column * (cellSize + cellSpacing),
                            gridOrigin.y + row * (cellSize + cellSpacing));
            const ImVec2 max(min.x + cellSize, min.y + cellSize);
            drawList->AddRectFilled(min, max, fillColor, 8.0f);
            drawList->AddRect(min, max, IM_COL32(90, 95, 110, 255), 8.0f, 0, 1.5f);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            drawList->AddText(ImVec2(min.x + (cellSize - textSize.x) * 0.5f, min.y + (cellSize - textSize.y) * 0.5f),
                            IM_COL32(235, 240, 245, 255),
                            label);
        };

        drawCell(0, 0, simulation.obstacles.frontLeft ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "FL");
        drawCell(0, 1, simulation.obstacles.front ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "F");
        drawCell(0, 2, simulation.obstacles.frontRight ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "FR");
        drawCell(1, 0, simulation.obstacles.left ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "L");

        const auto verticalState = simulation.obstacles.vertical;
        const char *centerLabel = "DRN";
        ImU32 centerColor = IM_COL32(70, 78, 90, 255);
        if (verticalState == protocol::VerticalObstacle::ABOVE)
        {
            centerLabel = "UP";
            centerColor = IM_COL32(220, 140, 50, 255);
        }
        else if (verticalState == protocol::VerticalObstacle::BELOW)
        {
            centerLabel = "DN";
            centerColor = IM_COL32(220, 140, 50, 255);
        }
        drawCell(1, 1, centerColor, centerLabel);

        drawCell(1, 2, simulation.obstacles.right ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "R");
        drawCell(2, 0, simulation.obstacles.backLeft ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "BL");
        drawCell(2, 1, simulation.obstacles.back ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "B");
        drawCell(2, 2, simulation.obstacles.backRight ? IM_COL32(170, 60, 60, 255) : IM_COL32(50, 55, 65, 255), "BR");

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const ImVec2 mousePosition = ImGui::GetIO().MousePos;
            const float localX = mousePosition.x - gridOrigin.x;
            const float localY = mousePosition.y - gridOrigin.y;
            const int column = static_cast<int>(localX / (cellSize + cellSpacing));
            const int row = static_cast<int>(localY / (cellSize + cellSpacing));

            if (column >= 0 && column < 3 && row >= 0 && row < 3)
            {
                if (row == 0 && column == 0)
                    simulation.obstacles.frontLeft = !simulation.obstacles.frontLeft;
                else if (row == 0 && column == 1)
                    simulation.obstacles.front = !simulation.obstacles.front;
                else if (row == 0 && column == 2)
                    simulation.obstacles.frontRight = !simulation.obstacles.frontRight;
                else if (row == 1 && column == 0)
                    simulation.obstacles.left = !simulation.obstacles.left;
                else if (row == 1 && column == 1)
                {
                    if (verticalState == protocol::VerticalObstacle::NONE)
                        simulation.obstacles.vertical = protocol::VerticalObstacle::ABOVE;
                    else if (verticalState == protocol::VerticalObstacle::ABOVE)
                        simulation.obstacles.vertical = protocol::VerticalObstacle::BELOW;
                    else
                        simulation.obstacles.vertical = protocol::VerticalObstacle::NONE;
                }
                else if (row == 1 && column == 2)
                    simulation.obstacles.right = !simulation.obstacles.right;
                else if (row == 2 && column == 0)
                    simulation.obstacles.backLeft = !simulation.obstacles.backLeft;
                else if (row == 2 && column == 1)
                    simulation.obstacles.back = !simulation.obstacles.back;
                else if (row == 2 && column == 2)
                    simulation.obstacles.backRight = !simulation.obstacles.backRight;
                obstaclesChanged = true;
            }
        }

        if (obstaclesChanged)
        {
            sharedState.setSimulationModel(simulation);
            protocolClient.sendSimulationObstacles(simulation.obstacles);
        }

        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        bool lidarActive = simulation.lidar.lidarActive;
        if (ImGui::Checkbox("LiDAR", &lidarActive))
        {
            simulation.lidar.lidarActive = lidarActive;
            sharedState.setSimulationModel(simulation);
            protocolClient.sendSimulationLidar(lidarActive);
        }

        ImGui::End();
    }

} // namespace gcs