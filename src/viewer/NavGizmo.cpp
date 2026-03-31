#include "viewer/NavGizmo.h"
#include "viewer/OrbitCamera.h"

#include "core/PathUtils.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/trigonometric.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace gcs::viewer
{

    const NavGizmo::AxisNode NavGizmo::kAxes[NavGizmo::AXIS_COUNT] =
    {
        { glm::vec3( 1.0f, 0.0f, 0.0f), glm::vec4(1.0f, 0.2f, 0.32f, 1.0f), "X"  },
        { glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec4(0.6f, 0.12f, 0.19f, 1.0f), "-X" },
        { glm::vec3( 0.0f, 1.0f, 0.0f), glm::vec4(0.55f, 0.86f, 0.0f, 1.0f), "Y"  },
        { glm::vec3( 0.0f,-1.0f, 0.0f), glm::vec4(0.33f, 0.53f, 0.0f, 1.0f), "-Y" },
        { glm::vec3( 0.0f, 0.0f, 1.0f), glm::vec4(0.16f, 0.56f, 1.0f, 1.0f), "Z"  },
        { glm::vec3( 0.0f, 0.0f,-1.0f), glm::vec4(0.09f, 0.34f, 0.6f, 1.0f), "-Z" },
    };

    NavGizmo::~NavGizmo()
    {
        shutdown();
    }

    bool NavGizmo::initialize(const std::filesystem::path& shaderDir)
    {
        if (!createShaderProgram(shaderDir))
            return false;

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, x)));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, r)));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        return true;
    }

    void NavGizmo::shutdown()
    {
        if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
        if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
        if (shaderProgram) { glDeleteProgram(shaderProgram); shaderProgram = 0; }
    }

    bool NavGizmo::createShaderProgram(const std::filesystem::path& shaderDir)
    {
        unsigned int vs = compileShader(GL_VERTEX_SHADER, loadShaderSource("nav_gizmo.vert"));
        unsigned int fs = compileShader(GL_FRAGMENT_SHADER, loadShaderSource("nav_gizmo.frag"));
        if (!vs || !fs) return false;

        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vs);
        glAttachShader(shaderProgram, fs);
        glLinkProgram(shaderProgram);

        int ok;
        glGetProgramiv(shaderProgram, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[512];
            glGetProgramInfoLog(shaderProgram, sizeof(log), nullptr, log);
            glDeleteShader(vs);
            glDeleteShader(fs);
            return false;
        }

        glDeleteShader(vs);
        glDeleteShader(fs);

        uniformMvp = glGetUniformLocation(shaderProgram, "uMvp");
        return true;
    }

    unsigned int NavGizmo::compileShader(unsigned int shaderType, const std::string& source) const
    {
        unsigned int s = glCreateShader(shaderType);
        const char* srcPtr = source.c_str();
        glShaderSource(s, 1, &srcPtr, nullptr);
        glCompileShader(s);

        int ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[512];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    }

    std::string NavGizmo::loadShaderSource(const std::string& fileName) const
    {
        std::filesystem::path path = std::filesystem::path("resources") / "shaders" / fileName;
        std::ifstream file(path);
        if (!file.is_open())
            throw std::runtime_error("Failed to open shader: " + path.string());
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    int NavGizmo::handleClick(
        float mouseX, float mouseY,
        float gizmoCenterX, float gizmoCenterY,
        float gizmoSize,
        const OrbitCamera& camera) const
    {
        const float hitRadius = gizmoSize * 0.18f;
        const float axisLen = gizmoSize * 0.35f;

        glm::vec3 forward = camera.getForwardVector();
        glm::vec3 right = camera.getRightVector();
        glm::vec3 up = camera.getUpVector();

        int closestAxis = -1;
        float closestDist = hitRadius;

        for (int i = 0; i < AXIS_COUNT; ++i)
        {
            const auto& axis = kAxes[i];
            glm::vec3 dir = axis.direction;

            float sx = gizmoCenterX + glm::dot(dir, right) * axisLen;
            float sy = gizmoCenterY - glm::dot(dir, up) * axisLen;

            float dx = mouseX - sx;
            float dy = mouseY - sy;
            float dist = glm::sqrt(dx * dx + dy * dy);

            if (dist < closestDist)
            {
                closestDist = dist;
                closestAxis = i;
            }
        }

        return closestAxis;
    }

    void NavGizmo::render(
        const OrbitCamera& camera,
        float gizmoCenterX, float gizmoCenterY,
        float gizmoSize,
        int fbWidth, int fbHeight,
        int visibleAxesMask)
    {
        if (!shaderProgram || !vao) return;

        vertices.clear();

        glm::vec3 forward = camera.getForwardVector();
        glm::vec3 right = camera.getRightVector();
        glm::vec3 up = camera.getUpVector();

        const float nodeRadius = gizmoSize * 0.10f;
        const float axisLen = gizmoSize * 0.35f;
        const float lineThick = 4.5f;
        const float centerRadius = gizmoSize * 0.06f;

        auto addCircle = [&](float cx, float cy, float radius, glm::vec4 color)
        {
            vertices.push_back({ cx, cy, 0.0f, color.r, color.g, color.b, color.a });
            for (int i = 0; i <= kCircleSegments; ++i)
            {
                float angle = (static_cast<float>(i) / kCircleSegments) * 2.0f * glm::pi<float>();
                vertices.push_back({
                    cx + radius * glm::cos(angle),
                    cy + radius * glm::sin(angle),
                    0.0f,
                    color.r, color.g, color.b, color.a
                });
            }
        };

        auto addLine = [&](float x0, float y0, float x1, float y1, float thickness, glm::vec4 color)
        {
            glm::vec2 dir(x1 - x0, y1 - y0);
            float len = glm::length(dir);
            if (len < 0.001f) return;
            dir /= len;
            glm::vec2 perp(-dir.y, dir.x);
            float h = thickness * 0.5f;

            vertices.push_back({ x0 - perp.x * h, y0 - perp.y * h, 0.0f, color.r, color.g, color.b, color.a });
            vertices.push_back({ x0 + perp.x * h, y0 + perp.y * h, 0.0f, color.r, color.g, color.b, color.a });
            vertices.push_back({ x1 - perp.x * h, y1 - perp.y * h, 0.0f, color.r, color.g, color.b, color.a });
            vertices.push_back({ x0 + perp.x * h, y0 + perp.y * h, 0.0f, color.r, color.g, color.b, color.a });
            vertices.push_back({ x1 + perp.x * h, y1 + perp.y * h, 0.0f, color.r, color.g, color.b, color.a });
            vertices.push_back({ x1 - perp.x * h, y1 - perp.y * h, 0.0f, color.r, color.g, color.b, color.a });
        };

        struct AxisDrawOrder
        {
            int index;
            float depth;
        };

        AxisDrawOrder order[AXIS_COUNT];
        for (int i = 0; i < AXIS_COUNT; ++i)
        {
            order[i].index = i;
            order[i].depth = glm::dot(kAxes[i].direction, forward);
        }

        for (int a = 0; a < AXIS_COUNT - 1; ++a)
        {
            for (int b = a + 1; b < AXIS_COUNT; ++b)
            {
                if (order[b].depth > order[a].depth)
                {
                    AxisDrawOrder tmp = order[a];
                    order[a] = order[b];
                    order[b] = tmp;
                }
            }
        }

        for (int o = 0; o < AXIS_COUNT; ++o)
        {
            int i = order[o].index;
            // if (!(visibleAxesMask & (1 << i)))
            //     continue;

            const auto& axis = kAxes[i];
            float sx = gizmoCenterX + glm::dot(axis.direction, right) * axisLen;
            float sy = gizmoCenterY + glm::dot(axis.direction, up) * axisLen;
            addCircle(sx, sy, nodeRadius, axis.color);
        }

        int lineVerts = 0;
        for (int i = 0; i < 3; ++i)
        {
            const auto& axis = kAxes[i * 2];
            float ex = gizmoCenterX + glm::dot(axis.direction, right) * axisLen;
            float ey = gizmoCenterY + glm::dot(axis.direction, up) * axisLen;
            int vertsBefore = static_cast<int>(vertices.size());
            addLine(gizmoCenterX, gizmoCenterY, ex, ey, lineThick, axis.color);
            lineVerts += static_cast<int>(vertices.size()) - vertsBefore;
        }

        addCircle(gizmoCenterX, gizmoCenterY, centerRadius, glm::vec4(0.35f, 0.35f, 0.35f, 1.0f));

        int totalVerts = static_cast<int>(vertices.size());
        if (totalVerts > bufferCapacity)
        {
            bufferCapacity = std::max(totalVerts, bufferCapacity == 0 ? 256 : bufferCapacity * 2);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         bufferCapacity * static_cast<int>(sizeof(Vertex)),
                         nullptr, GL_DYNAMIC_DRAW);
        }

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        totalVerts * static_cast<int>(sizeof(Vertex)),
                        vertices.data());

        glm::mat4 ortho = glm::ortho(
            0.0f, static_cast<float>(fbWidth),
            0.0f, static_cast<float>(fbHeight),
            -1.0f, 1.0f);

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(uniformMvp, 1, GL_FALSE, glm::value_ptr(ortho));

        glBindVertexArray(vao);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        int offset = 0;

        for (int i = 0; i < AXIS_COUNT; ++i)
        {
            int count = kCircleSegments + 2;
            glDrawArrays(GL_TRIANGLE_FAN, offset, count);
            offset += count;
        }

        glDrawArrays(GL_TRIANGLES, offset, lineVerts);

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        glBindVertexArray(0);
        glUseProgram(0);
    }

}
