#include "viewer/OverlayRenderer.h"

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace gcs::viewer
{

    // ═══════════════════════════════════════════
    // Инициализация / Уничтожение
    // ═══════════════════════════════════════════

    OverlayRenderer::~OverlayRenderer()
    {
        shutdown();
    }

    bool OverlayRenderer::initialize()
    {
        if (shaderProgram != 0) return true;

        if (!createShaderProgram()) return false;

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        // Позиция: location 0
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                             reinterpret_cast<void*>(0));

        // Цвет: location 1
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                             reinterpret_cast<void*>(3 * sizeof(float)));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return true;
    }

    void OverlayRenderer::shutdown()
    {
        if (vbo != 0)    { glDeleteBuffers(1, &vbo); vbo = 0; }
        if (vao != 0)    { glDeleteVertexArrays(1, &vao); vao = 0; }
        if (shaderProgram != 0) { glDeleteProgram(shaderProgram); shaderProgram = 0; }
    }

    bool OverlayRenderer::createShaderProgram()
    {
        // Встроенные шейдеры (можно вынести в файлы + fallback)
        const char* vertSrc = R"(#version 330 core
            layout (location = 0) in vec3 inPosition;
            layout (location = 1) in vec4 inColor;
            uniform mat4 uMvp;
            out vec4 vColor;
            void main() {
                gl_Position = uMvp * vec4(inPosition, 1.0);
                vColor = inColor;
            }
        )";

        const char* fragSrc = R"(#version 330 core
            in vec4 vColor;
            out vec4 fragColor;
            void main() {
                fragColor = vColor;
            }
        )";

        auto compile = [](unsigned int type, const char* src) -> unsigned int {
            unsigned int s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            int ok = GL_FALSE;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (ok != GL_TRUE) { glDeleteShader(s); return 0; }
            return s;
        };

        unsigned int vs = compile(GL_VERTEX_SHADER, vertSrc);
        unsigned int fs = compile(GL_FRAGMENT_SHADER, fragSrc);
        if (vs == 0 || fs == 0)
        {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            return false;
        }

        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vs);
        glAttachShader(shaderProgram, fs);
        glLinkProgram(shaderProgram);
        glDeleteShader(vs);
        glDeleteShader(fs);

        int linkOk = GL_FALSE;
        glGetProgramiv(shaderProgram, GL_LINK_STATUS, &linkOk);
        if (linkOk != GL_TRUE)
        {
            glDeleteProgram(shaderProgram);
            shaderProgram = 0;
            return false;
        }

        uniformMvp = glGetUniformLocation(shaderProgram, "uMvp");
        return true;
    }

    // ═══════════════════════════════════════════
    // Основной рендер
    // ═══════════════════════════════════════════

    void OverlayRenderer::render(const SceneOverlay& overlay,
                                 const glm::mat4& mvp,
                                 const glm::vec3& cameraPos)
    {
        if (shaderProgram == 0 || overlay.empty()) return;

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(uniformMvp, 1, GL_FALSE, glm::value_ptr(mvp));

        // ── Шаг 1: Непрозрачные линии (depth write ON) ──
        renderLines(overlay, mvp);
        renderArrows(overlay, mvp);

        // ── Шаг 2: Рёбра кубов (depth write ON) ──
        renderBoxEdges(overlay, mvp);

        // ── Шаг 3: Полупрозрачные грани кубов (depth write OFF) ──
        renderBoxFaces(overlay, mvp, cameraPos);

        glUseProgram(0);
    }

    // ═══════════════════════════════════════════
    // Линии
    // ═══════════════════════════════════════════

    void OverlayRenderer::renderLines(const SceneOverlay& overlay, 
                                       const glm::mat4& /*mvp*/)
    {
        if (overlay.lines.empty()) return;

        vertexBuffer.clear();
        for (const auto& line : overlay.lines)
        {
            vertexBuffer.push_back({
                line.start.x, line.start.y, line.start.z,
                line.color.r, line.color.g, line.color.b, line.color.a
            });
            vertexBuffer.push_back({
                line.end.x, line.end.y, line.end.z,
                line.color.r, line.color.g, line.color.b, line.color.a
            });
        }

        // Все линии одной толщины для простоты
        float thickness = overlay.lines.empty() ? 1.0f : overlay.lines[0].thickness;
        uploadAndDraw(vertexBuffer, GL_LINES, thickness);
    }

    // ═══════════════════════════════════════════
    // Стрелки (линия + наконечник)
    // ═══════════════════════════════════════════

    void OverlayRenderer::renderArrows(const SceneOverlay& overlay, 
                                        const glm::mat4& /*mvp*/)
    {
        if (overlay.arrows.empty()) return;

        vertexBuffer.clear();
        for (const auto& arrow : overlay.arrows)
        {
            buildArrowVertices(arrow, vertexBuffer);
        }

        uploadAndDraw(vertexBuffer, GL_LINES, 2.0f);
    }

    void OverlayRenderer::buildArrowVertices(const Arrow& arrow, 
                                              std::vector<Vertex>& out)
    {
        glm::vec3 end = arrow.origin + arrow.direction;
        float len = glm::length(arrow.direction);
        if (len < 0.001f) return;

        glm::vec3 dir = arrow.direction / len;

        // Основная линия
        out.push_back({
            arrow.origin.x, arrow.origin.y, arrow.origin.z,
            arrow.color.r, arrow.color.g, arrow.color.b, arrow.color.a
        });
        out.push_back({
            end.x, end.y, end.z,
            arrow.color.r, arrow.color.g, arrow.color.b, arrow.color.a
        });

        // Наконечник: два отрезка образуют "V"
        // Находим перпендикуляр к направлению
        glm::vec3 perp = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(dir, perp)) > 0.99f)
        {
            perp = glm::vec3(1.0f, 0.0f, 0.0f);
        }
        glm::vec3 side = glm::normalize(glm::cross(dir, perp));
        glm::vec3 up = glm::normalize(glm::cross(side, dir));

        float headLen = len * arrow.headSize;
        glm::vec3 headBase = end - dir * headLen;

        float headWidth = headLen * 0.4f;

        // Четыре линии наконечника (пирамидка)
        glm::vec3 tips[4] = {
            headBase + side * headWidth,
            headBase - side * headWidth,
            headBase + up * headWidth,
            headBase - up * headWidth,
        };

        for (const auto& tip : tips)
        {
            out.push_back({
                end.x, end.y, end.z,
                arrow.color.r, arrow.color.g, arrow.color.b, arrow.color.a
            });
            out.push_back({
                tip.x, tip.y, tip.z,
                arrow.color.r, arrow.color.g, arrow.color.b, arrow.color.a
            });
        }
    }

    // ═══════════════════════════════════════════
    // Рёбра кубов (wireframe)
    // ═══════════════════════════════════════════

    void OverlayRenderer::renderBoxEdges(const SceneOverlay& overlay, 
                                          const glm::mat4& /*mvp*/)
    {
        if (overlay.boxes.empty()) return;

        vertexBuffer.clear();
        for (const auto& box : overlay.boxes)
        {
            buildBoxEdgeVertices(box, vertexBuffer);
        }

        uploadAndDraw(vertexBuffer, GL_LINES, 
                      overlay.boxes[0].edgeThickness);
    }

    void OverlayRenderer::buildBoxEdgeVertices(const Box& box, 
                                                std::vector<Vertex>& out)
    {
        //    7 ─── 6        Y
        //   /|    /|        ↑
        //  4 ─── 5 |        │
        //  | 3 ──│ 2        └──→ X
        //  |/    |/        /
        //  0 ─── 1        Z

        const glm::vec3 mn = box.center - box.halfExtents;
        const glm::vec3 mx = box.center + box.halfExtents;
        const auto& c = box.edgeColor;

        glm::vec3 v[8] = {
            {mn.x, mn.y, mx.z},  // 0: front-bottom-left
            {mx.x, mn.y, mx.z},  // 1: front-bottom-right
            {mx.x, mn.y, mn.z},  // 2: back-bottom-right
            {mn.x, mn.y, mn.z},  // 3: back-bottom-left
            {mn.x, mx.y, mx.z},  // 4: front-top-left
            {mx.x, mx.y, mx.z},  // 5: front-top-right
            {mx.x, mx.y, mn.z},  // 6: back-top-right
            {mn.x, mx.y, mn.z},  // 7: back-top-left
        };

        // 12 рёбер = 24 вершины для GL_LINES
        const int edges[][2] = {
            // Bottom face
            {0,1}, {1,2}, {2,3}, {3,0},
            // Top face
            {4,5}, {5,6}, {6,7}, {7,4},
            // Vertical edges
            {0,4}, {1,5}, {2,6}, {3,7},
        };

        for (const auto& edge : edges)
        {
            const auto& a = v[edge[0]];
            const auto& b = v[edge[1]];
            out.push_back({a.x, a.y, a.z, c.r, c.g, c.b, c.a});
            out.push_back({b.x, b.y, b.z, c.r, c.g, c.b, c.a});
        }
    }

    // ═══════════════════════════════════════════
    // Полупрозрачные грани кубов
    // ═══════════════════════════════════════════

    void OverlayRenderer::renderBoxFaces(const SceneOverlay& overlay,
                                          const glm::mat4& /*mvp*/,
                                          const glm::vec3& cameraPos)
    {
        if (overlay.boxes.empty()) return;

        // ── Сортировка back-to-front ──
        // Копируем индексы и сортируем по расстоянию до камеры
        std::vector<std::size_t> sortedIndices(overlay.boxes.size());
        for (std::size_t i = 0; i < sortedIndices.size(); ++i)
        {
            sortedIndices[i] = i;
        }

        std::sort(sortedIndices.begin(), sortedIndices.end(),
            [&](std::size_t a, std::size_t b)
            {
                float distA = glm::length(overlay.boxes[a].center - cameraPos);
                float distB = glm::length(overlay.boxes[b].center - cameraPos);
                return distA > distB;  // Дальние первыми
            });

        // ── Генерация вершин в отсортированном порядке ──
        vertexBuffer.clear();
        for (std::size_t idx : sortedIndices)
        {
            // Пропускаем полностью непрозрачные (нет смысла рисовать грани)
            if (overlay.boxes[idx].faceColor.a <= 0.001f) continue;

            buildBoxFaceVertices(overlay.boxes[idx], vertexBuffer);
        }

        if (vertexBuffer.empty()) return;

        // ── Рендер с полупрозрачностью ──
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);  // ← Не пишем в depth buffer!
        // Depth test остаётся ON — грани за непрозрачными объектами не рисуются

        // Рисуем обе стороны граней (видно и изнутри куба)
        glDisable(GL_CULL_FACE);

        uploadAndDraw(vertexBuffer, GL_TRIANGLES);

        // ── Восстановление состояния ──
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
    }

    void OverlayRenderer::buildBoxFaceVertices(const Box& box, 
                                                std::vector<Vertex>& out)
    {
        const glm::vec3 mn = box.center - box.halfExtents;
        const glm::vec3 mx = box.center + box.halfExtents;
        const auto& c = box.faceColor;

        glm::vec3 v[8] = {
            {mn.x, mn.y, mx.z},  // 0
            {mx.x, mn.y, mx.z},  // 1
            {mx.x, mn.y, mn.z},  // 2
            {mn.x, mn.y, mn.z},  // 3
            {mn.x, mx.y, mx.z},  // 4
            {mx.x, mx.y, mx.z},  // 5
            {mx.x, mx.y, mn.z},  // 6
            {mn.x, mx.y, mn.z},  // 7
        };

        // 6 граней × 2 треугольника × 3 вершины = 36 вершин
        const int faces[][4] = {
            {0, 1, 5, 4},  // Front  (+Z)
            {2, 3, 7, 6},  // Back   (-Z)
            {1, 2, 6, 5},  // Right  (+X)
            {3, 0, 4, 7},  // Left   (-X)
            {4, 5, 6, 7},  // Top    (+Y)
            {3, 2, 1, 0},  // Bottom (-Y)
        };

        for (const auto& face : faces)
        {
            // Треугольник 1: v[0], v[1], v[2]
            const auto& a = v[face[0]];
            const auto& b = v[face[1]];
            const auto& d = v[face[2]];
            out.push_back({a.x, a.y, a.z, c.r, c.g, c.b, c.a});
            out.push_back({b.x, b.y, b.z, c.r, c.g, c.b, c.a});
            out.push_back({d.x, d.y, d.z, c.r, c.g, c.b, c.a});

            // Треугольник 2: v[0], v[2], v[3]
            const auto& e = v[face[3]];
            out.push_back({a.x, a.y, a.z, c.r, c.g, c.b, c.a});
            out.push_back({d.x, d.y, d.z, c.r, c.g, c.b, c.a});
            out.push_back({e.x, e.y, e.z, c.r, c.g, c.b, c.a});
        }
    }

    // ═══════════════════════════════════════════
    // Общая загрузка и отрисовка
    // ═══════════════════════════════════════════

    void OverlayRenderer::uploadAndDraw(const std::vector<Vertex>& vertices,
                                         unsigned int mode,
                                         float lineWidth)
    {
        if (vertices.empty()) return;

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<long long>(vertices.size() * sizeof(Vertex)),
                     vertices.data(),
                     GL_DYNAMIC_DRAW);

        if (mode == GL_LINES)
        {
            glLineWidth(lineWidth);
        }

        glDrawArrays(mode, 0, static_cast<int>(vertices.size()));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

}