#include "viewer/CubeGizmo.h"
#include "viewer/OrbitCamera.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace gcs::viewer
{

    // Билинейная интерполяция по 4 углам проецированной грани
    static ImVec2 bilinear(const ImVec2 p[4], float u, float v)
    {
        return ImVec2(
            p[0].x * (1 - u) * (1 - v) + p[1].x * (1 - u) * v +
            p[2].x * u * v + p[3].x * u * (1 - v),
            p[0].y * (1 - u) * (1 - v) + p[1].y * (1 - u) * v +
            p[2].y * u * v + p[3].y * u * (1 - v));
    }

    // Грани куба
    const CubeGizmo::Face CubeGizmo::faces[6] = {
        {"Справа", {1,0,0}, {0.5f,0,0},
         {{0.5f,-0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {0.5f,0.5f,0.5f}, {0.5f,-0.5f,0.5f}},
         IM_COL32(210, 65, 65, 255), 1.57079633f, 0.0f},
        {"Слева", {-1,0,0}, {-0.5f,0,0},
         {{-0.5f,-0.5f,0.5f}, {-0.5f,0.5f,0.5f}, {-0.5f,0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}},
         IM_COL32(155, 45, 45, 255), -1.57079633f, 0.0f},
        {"Сверху", {0,1,0}, {0,0.5f,0},
         {{-0.5f,0.5f,0.5f}, {0.5f,0.5f,0.5f}, {0.5f,0.5f,-0.5f}, {-0.5f,0.5f,-0.5f}},
         IM_COL32(65, 200, 65, 255), 0.0f, 1.57079633f},
        {"Снизу", {0,-1,0}, {0,-0.5f,0},
         {{-0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,0.5f}, {-0.5f,-0.5f,0.5f}},
         IM_COL32(45, 140, 45, 255), 0.0f, -1.57079633f},
        {"Спереди", {0,0,1}, {0,0,0.5f},
         {{-0.5f,-0.5f,0.5f}, {0.5f,-0.5f,0.5f}, {0.5f,0.5f,0.5f}, {-0.5f,0.5f,0.5f}},
         IM_COL32(65, 110, 220, 255), 0.0f, 0.0f},
        {"Сзади", {0,0,-1}, {0,0,-0.5f},
         {{0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f,0.5f,-0.5f}, {0.5f,0.5f,-0.5f}},
         IM_COL32(45, 80, 160, 255), 3.14159265f, 0.0f},
    };

    // Проецирование 3D → 2D
    ImVec2 CubeGizmo::project(const glm::vec3 &point,
                               const ImVec2 &center,
                               float scale,
                               const glm::vec3 &right,
                               const glm::vec3 &up)
    {
        const float x = glm::dot(point, right);
        const float y = glm::dot(point, up);
        return ImVec2(center.x + x * scale, center.y - y * scale);
    }

    // Основной рендер
    bool CubeGizmo::render(ImDrawList *drawList,
                            const ImVec2 &imageMin,
                            const ImVec2 &imageMax,
                            const OrbitCamera &camera,
                            OrbitCamera &cameraMutable)
    {
        const float gizmoSize = ImGui::GetFontSize() * 5.0f;
        const float padding = ImGui::GetFontSize() * 0.8f;
        const ImVec2 gizmoCenter(imageMax.x - padding - gizmoSize * 0.5f,
                                  imageMin.y + padding + gizmoSize * 0.5f);
        const float halfSize = gizmoSize * 0.5f;

        const glm::vec3 forward = camera.getForwardVector();
        const glm::vec3 cameraRight = camera.getRightVector();
        const glm::vec3 cameraUp = glm::normalize(glm::cross(forward, cameraRight));

        // Фон
        const ImVec2 bgMin(gizmoCenter.x - halfSize - 6, gizmoCenter.y - halfSize - 6);
        const ImVec2 bgMax(gizmoCenter.x + halfSize + 6, gizmoCenter.y + halfSize + 6);
        drawList->AddRectFilled(bgMin, bgMax, IM_COL32(20, 22, 28, 200), 6.0f);

        // Сортировка граней: дальние первыми
        int sortedFaces[6] = {0, 1, 2, 3, 4, 5};
        std::sort(sortedFaces, sortedFaces + 6, [&](int a, int b) {
            return glm::dot(faces[a].center, forward) < glm::dot(faces[b].center, forward);
        });

        // Текстура атласа
        ImTextureID atlasTex = ImGui::GetIO().Fonts->TexRef.GetTexID();

        bool clicked = false;
        float closestDist = 1e30f;
        int closestFace = -1;

        for (int idx = 0; idx < 6; ++idx)
        {
            const Face &face = faces[sortedFaces[idx]];
            int faceIdx = sortedFaces[idx];

            ImVec2 projected[4];
            for (int i = 0; i < 4; ++i)
                projected[i] = project(face.corners[i], gizmoCenter, halfSize, cameraRight, cameraUp);

            // Заливка грани
            drawList->AddConvexPolyFilled(projected, 4, face.color);

            // Обводка
            drawList->AddPolyline(projected, 4, IM_COL32(255, 255, 255, 80), ImDrawFlags_Closed, 1.0f);

            // Подпись на грани: текстурированные глифы через билинейную интерполяцию
            {
                ImFont *font = ImGui::GetFont();
                ImFontBaked *baked = font->GetFontBaked(ImGui::GetFontSize());
                if (baked)
                {
                    // Размер грани — среднее из 2 рёбер
                    float edgeLen01 = std::sqrt((projected[1].x - projected[0].x) * (projected[1].x - projected[0].x) +
                                                (projected[1].y - projected[0].y) * (projected[1].y - projected[0].y));
                    float edgeLen03 = std::sqrt((projected[3].x - projected[0].x) * (projected[3].x - projected[0].x) +
                                                (projected[3].y - projected[0].y) * (projected[3].y - projected[0].y));
                    float faceW = edgeLen03;
                    float faceH = edgeLen01;

                    // Ширина текста
                    float totalAdvance = 0.0f;
                    const char *s = face.label;
                    while (*s)
                    {
                        unsigned int c = 0;
                        int bytes = ImTextCharFromUtf8(&c, s, nullptr);
                        if (bytes <= 0) break;
                        s += bytes;
                        const ImFontGlyph *glyph = baked->FindGlyph((ImWchar)c);
                        if (glyph) totalAdvance += glyph->AdvanceX;
                    }

                    float fontSize = std::min(faceW, faceH) * 0.30f;
                    float scale = fontSize / baked->Size;
                    float textW = totalAdvance * scale;
                    float cursorX = (faceW - textW) * 0.5f;
                    float baselineY = (faceH - fontSize) * 0.5f + baked->Ascent * scale;

                    const char *txt = face.label;
                    while (*txt)
                    {
                        unsigned int cp = 0;
                        int bytes = ImTextCharFromUtf8(&cp, txt, nullptr);
                        if (bytes <= 0) break;
                        txt += bytes;

                        const ImFontGlyph *glyph = baked->FindGlyph((ImWchar)cp);
                        if (!glyph) { cursorX += 4.0f * scale; continue; }

                        float gx0 = cursorX + glyph->X0 * scale;
                        float gy0 = baselineY + glyph->Y0 * scale;
                        float gx1 = cursorX + glyph->X1 * scale;
                        float gy1 = baselineY + glyph->Y1 * scale;

                        ImVec2 p0 = bilinear(projected, gx0 / faceW, gy0 / faceH);
                        ImVec2 p1 = bilinear(projected, gx1 / faceW, gy0 / faceH);
                        ImVec2 p2 = bilinear(projected, gx1 / faceW, gy1 / faceH);
                        ImVec2 p3 = bilinear(projected, gx0 / faceW, gy1 / faceH);

                        drawList->AddImageQuad(
                            atlasTex,
                            p0, p1, p2, p3,
                            ImVec2(glyph->U0, glyph->V0),
                            ImVec2(glyph->U1, glyph->V0),
                            ImVec2(glyph->U1, glyph->V1),
                            ImVec2(glyph->U0, glyph->V1),
                            IM_COL32(255, 255, 255, 220));

                        cursorX += glyph->AdvanceX * scale;
                    }
                }
            }

            // Клик
            ImVec2 center2d(
                (projected[0].x + projected[1].x + projected[2].x + projected[3].x) * 0.25f,
                (projected[0].y + projected[1].y + projected[2].y + projected[3].y) * 0.25f);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                ImVec2 mousePos = ImGui::GetMousePos();
                if (mousePos.x >= bgMin.x && mousePos.x <= bgMax.x &&
                    mousePos.y >= bgMin.y && mousePos.y <= bgMax.y)
                {
                    float dist = (mousePos.x - center2d.x) * (mousePos.x - center2d.x)
                               + (mousePos.y - center2d.y) * (mousePos.y - center2d.y);
                    if (dist < closestDist)
                    {
                        closestDist = dist;
                        closestFace = faceIdx;
                    }
                }
            }
        }

        // Оси X/Y/Z — из ФИКСИРОВАННОГО угла (+0.5, +0.5, +0.5)
        {
            const glm::vec3 corner(0.5f, 0.5f, 0.5f);
            const ImVec2 corner2d = project(corner, gizmoCenter, halfSize, cameraRight, cameraUp);

            const glm::vec3 edgeEnds[3] = {
                glm::vec3(-0.5f, +0.5f, +0.5f),
                glm::vec3(+0.5f, -0.5f, +0.5f),
                glm::vec3(+0.5f, +0.5f, -0.5f),
            };
            const ImU32 axisColors[3] = {
                IM_COL32(255, 90, 90, 255),
                IM_COL32(110, 240, 110, 255),
                IM_COL32(110, 170, 255, 255),
            };
            const char *axisLabels[3] = {"X", "Y", "Z"};

            for (int i = 0; i < 3; ++i)
            {
                const ImVec2 end2d = project(edgeEnds[i], gizmoCenter, halfSize, cameraRight, cameraUp);
                drawList->AddLine(corner2d, end2d, axisColors[i], 2.5f);
                drawList->AddText(ImVec2(end2d.x + 3.0f, end2d.y - 7.0f), axisColors[i], axisLabels[i]);
            }
        }

        if (closestFace >= 0)
        {
            cameraMutable.setView(faces[closestFace].yaw, faces[closestFace].pitch);
            clicked = true;
        }

        return clicked;
    }

}