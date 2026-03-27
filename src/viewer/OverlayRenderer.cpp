#include "viewer/OverlayRenderer.h"
#include "core/PathUtils.h"

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace gcs::viewer
{

    OverlayRenderer::~OverlayRenderer()
    {
        shutdown();
    }

    bool OverlayRenderer::initialize(const std::filesystem::path& shaderDir)
    {
        if (shaderProgram != 0) return true;

        if (!createShaderProgram(shaderDir)) return false;

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

        // Нормаль: location 2
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                             reinterpret_cast<void*>(7 * sizeof(float)));

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

    bool OverlayRenderer::createShaderProgram(const std::filesystem::path& shaderDir)
    {
        const std::string vertSrc = readTextFile(shaderDir / "overlay.vert");
        const std::string fragSrc = readTextFile(shaderDir / "overlay.frag");

        auto compile = [](unsigned int type, const std::string& src) -> unsigned int {
            const char* srcPtr = src.c_str();
            unsigned int s = glCreateShader(type);
            glShaderSource(s, 1, &srcPtr, nullptr);
            glCompileShader(s);
            int ok = GL_FALSE;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (ok != GL_TRUE)
            {
                char log[512];
                glGetShaderInfoLog(s, sizeof(log), nullptr, log);
                std::cerr << "Shader compile error: " << log << std::endl;
                glDeleteShader(s);
                return 0;
            }
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
        uniformNormalSign = glGetUniformLocation(shaderProgram, "uNormalSign");
        return true;
    }

    void OverlayRenderer::render(const SceneOverlay& overlay,
                                 const glm::mat4& mvp,
                                 const glm::vec3& cameraPos)
    {
        if (shaderProgram == 0 || overlay.empty()) return;

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(uniformMvp, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform1f(uniformNormalSign, 1.0f);

        // Непрозрачные: линии, стрелки, рёбра кубов
        renderLines(overlay, mvp);
        renderArrows(overlay, mvp);
        renderBoxEdges(overlay, mvp);

        // Полупрозрачные грани кубов
        renderBoxFaces(overlay, mvp, cameraPos);

        glUseProgram(0);
    }

    void OverlayRenderer::renderLines(const SceneOverlay& overlay,  const glm::mat4& /*mvp*/)
    {
        if (overlay.lines.empty()) return;

        vertexBuffer.clear();
        for (const auto& line : overlay.lines)
        {
            vertexBuffer.push_back({
                line.start.x, line.start.y, line.start.z,
                line.color.r, line.color.g, line.color.b, line.color.a,
                0.0f, 0.0f, 0.0f
            });
            vertexBuffer.push_back({
                line.end.x, line.end.y, line.end.z,
                line.color.r, line.color.g, line.color.b, line.color.a,
                0.0f, 0.0f, 0.0f
            });
        }

        float thickness = overlay.lines.empty() ? 1.0f : overlay.lines[0].thickness;
        uploadAndDraw(vertexBuffer, GL_LINES, thickness);
    }

    // Стрелки (стержень + 3D-конус)
    void OverlayRenderer::renderArrows(const SceneOverlay& overlay, const glm::mat4& /*mvp*/)
    {
        if (overlay.arrows.empty()) return;

        // Стержень — линии
        vertexBuffer.clear();
        for (const auto& arrow : overlay.arrows)
        {
            buildArrowShaft(arrow, vertexBuffer);
        }
        uploadAndDraw(vertexBuffer, GL_LINES, 2.0f);

        // Конус — треугольники с освещением (непрозрачный)
        vertexBuffer.clear();
        for (const auto& arrow : overlay.arrows)
        {
            buildArrowCone(arrow, vertexBuffer);
        }
        if (!vertexBuffer.empty())
        {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glDisable(GL_CULL_FACE);
            glUniform1f(uniformNormalSign, -1.0f); // Инвертировать нормали конуса
            uploadAndDraw(vertexBuffer, GL_TRIANGLES);
            glUniform1f(uniformNormalSign, 1.0f); // Вернуть обратно
        }
    }

    void OverlayRenderer::buildArrowShaft(const Arrow& arrow, std::vector<Vertex>& out)
    {
        float len = glm::length(arrow.direction);
        if (len < 0.001f) return;

        glm::vec3 end = arrow.origin + arrow.direction;

        // Стержень — от origin до основания конуса
        float headLen = len * arrow.headSize;
        glm::vec3 dir = arrow.direction / len;
        glm::vec3 shaftEnd = end - dir * headLen; // Конец стержня = основание конуса

        out.push_back({
            arrow.origin.x, arrow.origin.y, arrow.origin.z,
            arrow.color.r, arrow.color.g, arrow.color.b, arrow.color.a,
            0.0f, 0.0f, 0.0f
        });
        out.push_back({
            shaftEnd.x, shaftEnd.y, shaftEnd.z,
            arrow.color.r, arrow.color.g, arrow.color.b, arrow.color.a,
            0.0f, 0.0f, 0.0f
        });
    }

    void OverlayRenderer::buildArrowCone(const Arrow& arrow, std::vector<Vertex>& out)
    {
        float len = glm::length(arrow.direction);
        if (len < 0.001f) return;

        glm::vec3 dir = arrow.direction / len;
        glm::vec3 tip = arrow.origin + arrow.direction;

        float headLen = len * arrow.headSize;
        float radius = headLen * 0.35f;

        // Базовый вектор конуса
        glm::vec3 base = tip - dir * headLen;

        // Два перпендикуляра к dir для построения окружности
        glm::vec3 perp(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(dir, perp)) > 0.99f)
            perp = glm::vec3(1.0f, 0.0f, 0.0f);

        glm::vec3 side = glm::normalize(glm::cross(dir, perp));
        glm::vec3 up = glm::normalize(glm::cross(side, dir));

        int segments = std::max(arrow.headSegments, 3);
        const auto& c = arrow.color;

        // Генерация конуса: каждый сегмент — треугольник (tip, base_i, base_{i+1})
        for (int i = 0; i < segments; ++i)
        {
            float angle0 = 2.0f * 3.14159265358979f * static_cast<float>(i) / static_cast<float>(segments);
            float angle1 = 2.0f * 3.14159265358979f * static_cast<float>(i + 1) / static_cast<float>(segments);

            glm::vec3 p0 = base + (side * std::cos(angle0) + up * std::sin(angle0)) * radius;
            glm::vec3 p1 = base + (side * std::cos(angle1) + up * std::sin(angle1)) * radius;

            // Нормаль из векторного произведения рёбер треугольника (tip, p0, p1)
            glm::vec3 edge1 = p0 - tip;
            glm::vec3 edge2 = p1 - tip;
            glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

            // Треугольник: tip, p0, p1 (нормаль наружу)
            out.push_back({tip.x, tip.y, tip.z, c.r, c.g, c.b, c.a,
                           normal.x, normal.y, normal.z});
            out.push_back({p0.x, p0.y, p0.z, c.r, c.g, c.b, c.a,
                           normal.x, normal.y, normal.z});
            out.push_back({p1.x, p1.y, p1.z, c.r, c.g, c.b, c.a,
                           normal.x, normal.y, normal.z});

            // Нижний колпачок (нормаль = -dir, порядок p1,p0 для правильной ориентации)
            out.push_back({base.x, base.y, base.z, c.r, c.g, c.b, c.a,
                           -dir.x, -dir.y, -dir.z});
            out.push_back({p1.x, p1.y, p1.z, c.r, c.g, c.b, c.a,
                           -dir.x, -dir.y, -dir.z});
            out.push_back({p0.x, p0.y, p0.z, c.r, c.g, c.b, c.a,
                           -dir.x, -dir.y, -dir.z});
        }
    }

    // Рёбра кубов (wireframe)
    void OverlayRenderer::renderBoxEdges(const SceneOverlay& overlay, const glm::mat4& /*mvp*/)
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

    void OverlayRenderer::buildBoxEdgeVertices(const Box& box, std::vector<Vertex>& out)
    {
        const glm::vec3 mn = box.center - box.halfExtents;
        const glm::vec3 mx = box.center + box.halfExtents;
        const auto& c = box.edgeColor;

        glm::vec3 v[8] = {
            {mn.x, mn.y, mx.z},
            {mx.x, mn.y, mx.z},
            {mx.x, mn.y, mn.z},
            {mn.x, mn.y, mn.z},
            {mn.x, mx.y, mx.z},
            {mx.x, mx.y, mx.z},
            {mx.x, mx.y, mn.z},
            {mn.x, mx.y, mn.z},
        };

        const int edges[][2] = {
            {0,1}, {1,2}, {2,3}, {3,0},
            {4,5}, {5,6}, {6,7}, {7,4},
            {0,4}, {1,5}, {2,6}, {3,7},
        };

        for (const auto& edge : edges)
        {
            const auto& a = v[edge[0]];
            const auto& b = v[edge[1]];
            out.push_back({a.x, a.y, a.z, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 0.0f});
            out.push_back({b.x, b.y, b.z, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 0.0f});
        }
    }

    // Полупрозрачные грани кубов
    void OverlayRenderer::renderBoxFaces(const SceneOverlay& overlay,
                                          const glm::mat4& /*mvp*/,
                                          const glm::vec3& cameraPos)
    {
        if (overlay.boxes.empty()) return;

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
                return distA > distB;
            });

        vertexBuffer.clear();
        for (std::size_t idx : sortedIndices)
        {
            if (overlay.boxes[idx].faceColor.a <= 0.001f) continue;
            buildBoxFaceVertices(overlay.boxes[idx], vertexBuffer);
        }

        if (vertexBuffer.empty()) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        uploadAndDraw(vertexBuffer, GL_TRIANGLES);

        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
    }

    void OverlayRenderer::buildBoxFaceVertices(const Box& box, std::vector<Vertex>& out)
    {
        const glm::vec3 mn = box.center - box.halfExtents;
        const glm::vec3 mx = box.center + box.halfExtents;
        const auto& c = box.faceColor;

        glm::vec3 v[8] = {
            {mn.x, mn.y, mx.z},
            {mx.x, mn.y, mx.z},
            {mx.x, mn.y, mn.z},
            {mn.x, mn.y, mn.z},
            {mn.x, mx.y, mx.z},
            {mx.x, mx.y, mx.z},
            {mx.x, mx.y, mn.z},
            {mn.x, mx.y, mn.z},
        };

        const int faces[][4] = {
            {0, 1, 5, 4},
            {2, 3, 7, 6},
            {1, 2, 6, 5},
            {3, 0, 4, 7},
            {4, 5, 6, 7},
            {3, 2, 1, 0},
        };

        for (const auto& face : faces)
        {
            const auto& a = v[face[0]];
            const auto& b = v[face[1]];
            const auto& d = v[face[2]];
            out.push_back({a.x, a.y, a.z, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 0.0f});
            out.push_back({b.x, b.y, b.z, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 0.0f});
            out.push_back({d.x, d.y, d.z, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 0.0f});

            const auto& e = v[face[3]];
            out.push_back({a.x, a.y, a.z, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 0.0f});
            out.push_back({d.x, d.y, d.z, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 0.0f});
            out.push_back({e.x, e.y, e.z, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 0.0f});
        }
    }

    // Общая загрузка и отрисовка
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