#include "viewer/OrbitCamera.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace gcs::viewer
{

namespace
{
constexpr float pi = 3.14159265358979323846f;
} // namespace

void OrbitCamera::handleInput(bool isHovered, const ImVec2 &viewportSize)
{
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
    yawRadians = 0.8f;
    pitchRadians = 0.45f;
    distance = 35.0f;
    target = glm::vec3(0.0f, 0.0f, 0.0f);
}

glm::mat4 OrbitCamera::buildViewMatrix() const
{
    return glm::lookAt(getPosition(), target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OrbitCamera::buildProjectionMatrix(float aspectRatio) const
{
    return glm::perspective(glm::radians(45.0f), std::max(aspectRatio, 0.1f), 0.1f, 2000.0f);
}

glm::vec3 OrbitCamera::getPosition() const
{
    const float cosPitch = std::cos(pitchRadians);
    const glm::vec3 offset{
        distance * std::sin(yawRadians) * cosPitch,
        distance * std::sin(pitchRadians),
        distance * std::cos(yawRadians) * cosPitch,
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
    return glm::normalize(glm::cross(getForwardVector(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 OrbitCamera::getUpVector() const
{
    return glm::normalize(glm::cross(getRightVector(), getForwardVector()));
}

} // namespace gcs::viewer
