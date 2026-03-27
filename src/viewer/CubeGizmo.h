#pragma once

#include <imgui.h>

#include <glm/vec3.hpp>

namespace gcs::viewer
{

    class OrbitCamera;

    class CubeGizmo
    {
    public:
        CubeGizmo() = default;
        ~CubeGizmo() = default;

        bool render(ImDrawList *drawList,
                    const ImVec2 &imageMin,
                    const ImVec2 &imageMax,
                    const OrbitCamera &camera,
                    OrbitCamera &cameraMutable);

    private:
        struct Face
        {
            const char *label;
            glm::vec3 normal;
            glm::vec3 center;
            glm::vec3 corners[4];
            ImU32 color;
            float yaw;
            float pitch;
        };

        static const Face faces[6];

        static ImVec2 project(const glm::vec3 &point,
                              const ImVec2 &center,
                              float scale,
                              const glm::vec3 &right,
                              const glm::vec3 &up);
    };

}