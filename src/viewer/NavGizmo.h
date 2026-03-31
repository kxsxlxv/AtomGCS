#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <glad/gl.h>

#include <filesystem>
#include <vector>

namespace gcs::viewer
{

    class OrbitCamera;

    class NavGizmo
    {
    public:
        NavGizmo() = default;
        ~NavGizmo();

        NavGizmo(const NavGizmo&) = delete;
        NavGizmo& operator=(const NavGizmo&) = delete;

        bool initialize(const std::filesystem::path& shaderDir);
        void shutdown();

        [[nodiscard]] int handleClick(
            float mouseX, float mouseY,
            float gizmoCenterX, float gizmoCenterY,
            float gizmoSize,
            const OrbitCamera& camera) const;

        void render(
            const OrbitCamera& camera,
            float gizmoCenterX, float gizmoCenterY,
            float gizmoSize,
            int fbWidth, int fbHeight,
            int visibleAxesMask = -1);

        enum AxisIndex : int
        {
            POS_X = 0,
            NEG_X = 1,
            POS_Y = 2,
            NEG_Y = 3,
            POS_Z = 4,
            NEG_Z = 5,
            AXIS_COUNT = 6
        };

    private:
        struct Vertex
        {
            float x, y, z;
            float r, g, b, a;
        };

        static const int kCircleSegments = 24;

        struct AxisNode
        {
            glm::vec3 direction;
            glm::vec4 color;
            const char* label;
        };

        static const AxisNode kAxes[AXIS_COUNT];

        bool createShaderProgram(const std::filesystem::path& shaderDir);
        unsigned int compileShader(unsigned int shaderType, const std::string& source) const;
        std::string loadShaderSource(const std::string& fileName) const;

        unsigned int shaderProgram = 0;
        unsigned int vao = 0;
        unsigned int vbo = 0;

        int uniformMvp = -1;

        int bufferCapacity = 0;
        std::vector<Vertex> vertices;
    };

}
