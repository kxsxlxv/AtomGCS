#include "viewer/OrbitCamera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cmath>

namespace gcs::viewer
{

    void OrbitCamera::handleInput(bool isHovered, const ImVec2 &viewportSize)
    {
        if (isAnimating)
        {
            return;
        }

        if (!isHovered || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
        {
            return;
        }

        ImGuiIO &io = ImGui::GetIO();

        if (io.MouseWheel != 0.0f)
        {
            const float zoomFactor = 1.0f - io.MouseWheel * 0.1f;
            distance = std::clamp(distance * zoomFactor, 2.0f, 500.0f);
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            yawRadians -= io.MouseDelta.x * 0.01f;
            pitchRadians += io.MouseDelta.y * 0.01f;
            pitchRadians = std::clamp(pitchRadians, -1.4f, 1.4f);
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            const glm::vec3 right = getRightVector();
            const glm::vec3 up = getUpVector();
            const float panScale = distance * 0.0025f;
            target -= right * io.MouseDelta.x * panScale;
            target += up * io.MouseDelta.y * panScale;
        }
    }

    void OrbitCamera::reset()
    {
        yawRadians = 0.0f;
        pitchRadians = 0.0f;
        distance = 35.0f;
        target = glm::vec3(0.0f, 0.0f, 0.0f);
        isAnimating = false;
    }

    void OrbitCamera::setView(float yaw, float pitch)
    {
        yawRadians = yaw;
        pitchRadians = std::clamp(pitch, -1.4f, 1.4f);
        isAnimating = false;
    }

    void OrbitCamera::updateAnimation(float deltaTime)
    {
        if (!isAnimating)
        {
            return;
        }

        animProgress += deltaTime / animDuration;

        if (animProgress >= 1.0f)
        {
            animProgress = 1.0f;
            isAnimating = false;

            glm::vec3 fwd = glm::normalize(animTargetForward);
            yawRadians = std::atan2(fwd.y, fwd.x);
            pitchRadians = std::clamp(std::asin(std::clamp(fwd.z, -1.0f, 1.0f)), -1.4f, 1.4f);
            return;
        }

        float t = animProgress;
        t = t * t * (3.0f - 2.0f * t);

        glm::vec3 fwd = glm::normalize(glm::mix(animStartForward, animTargetForward, t));
        yawRadians = std::atan2(fwd.y, fwd.x);
        pitchRadians = std::clamp(std::asin(std::clamp(fwd.z, -1.0f, 1.0f)), -1.4f, 1.4f);
    }

    void OrbitCamera::startSnapToAxis(int axisIndex)
    {
        glm::vec3 fwd = getForwardVector();
        animStartForward = fwd;

        glm::vec3 snapForward;

        switch (axisIndex)
        {
        case 0: snapForward = glm::vec3( 1.0f, 0.0f, 0.0f); break;
        case 1: snapForward = glm::vec3(-1.0f, 0.0f, 0.0f); break;
        case 2: snapForward = glm::vec3( 0.0f, 1.0f, 0.0f); break;
        case 3: snapForward = glm::vec3( 0.0f,-1.0f, 0.0f); break;
        case 4: snapForward = glm::vec3( 0.0f, 0.0f, 1.0f); break;
        case 5: snapForward = glm::vec3( 0.0f, 0.0f,-1.0f); break;
        default: return;
        }

        animTargetForward = snapForward;
        animProgress = 0.0f;
        isAnimating = true;
    }

    glm::mat4 OrbitCamera::buildViewMatrix() const
    {
        return glm::lookAt(getPosition(), target, glm::vec3(0.0f, 0.0f, 1.0f));
    }

    glm::mat4 OrbitCamera::buildProjectionMatrix(float aspectRatio) const
    {
        return glm::perspective(glm::radians(45.0f), std::max(aspectRatio, 0.1f), 0.1f, 2000.0f);
    }

    glm::vec3 OrbitCamera::getPosition() const
    {
        const float cosPitch = std::cos(pitchRadians);
        const glm::vec3 offset{
            -distance * std::cos(yawRadians) * cosPitch,
            -distance * std::sin(yawRadians) * cosPitch,
             distance * std::sin(pitchRadians),
        };
        return target + offset;
    }

    glm::vec3 OrbitCamera::getTarget() const
    {
        return target;
    }

    glm::vec3 OrbitCamera::getForwardVector() const
    {
        return glm::normalize(target - getPosition());
    }

    glm::vec3 OrbitCamera::getRightVector() const
    {
        return glm::normalize(glm::cross(getForwardVector(), glm::vec3(0.0f, 0.0f, 1.0f)));
    }

    glm::vec3 OrbitCamera::getUpVector() const
    {
        return glm::normalize(glm::cross(getRightVector(), getForwardVector()));
    }

}
