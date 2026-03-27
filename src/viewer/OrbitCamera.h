#pragma once

#include <imgui.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace gcs::viewer
{

    class OrbitCamera
    {
    public:
        OrbitCamera() = default;

        void handleInput(bool isHovered, const ImVec2 &viewportSize);
        void reset();
        void setView(float yaw, float pitch);

        [[nodiscard]] glm::mat4 buildViewMatrix() const;
        [[nodiscard]] glm::mat4 buildProjectionMatrix(float aspectRatio) const;

        [[nodiscard]] glm::vec3 getPosition() const;
        [[nodiscard]] glm::vec3 getTarget() const;
        [[nodiscard]] glm::vec3 getRightVector() const;
        [[nodiscard]] glm::vec3 getUpVector() const;
        [[nodiscard]] glm::vec3 getForwardVector() const;

    private:
        float yawRadians = 0.8f;
        float pitchRadians = 0.45f;
        float distance = 35.0f;
        glm::vec3 target{0.0f, 0.0f, 0.0f};
    };

}