#include "viewer/PointCloudRenderer.h"

#include "graphics/GlLoader.h"

#include <glad/gl.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace gcs::viewer
{

namespace
{
constexpr const char *fallbackVertexShader = R"(#version 330 core
layout (location = 0) in vec3 inPosition;
layout (location = 1) in float inIntensity;
layout (location = 2) in float inDistance;

uniform mat4 uMvp;
uniform float uPointSize;
uniform float uMaxDistance;

out float vIntensity;
out float vDistance;

void main()
{
    gl_Position = uMvp * vec4(inPosition, 1.0);
    gl_PointSize = uPointSize;
    vIntensity = clamp(inIntensity / 255.0, 0.0, 1.0);
    vDistance = clamp(inDistance / max(uMaxDistance, 0.001), 0.0, 1.0);
}
)";

constexpr const char *fallbackFragmentShader = R"(#version 330 core
in float vIntensity;
in float vDistance;

uniform int uColorMode;

out vec4 fragColor;

vec3 heatMap(float t)
{
    return mix(vec3(0.10, 0.35, 0.95), vec3(0.95, 0.15, 0.10), 1.0 - t);
}

void main()
{
    float value = (uColorMode == 0) ? vIntensity : vDistance;
    fragColor = vec4(heatMap(value), 1.0);
}
)";

glm::vec3 convertNedToViewer(const protocol::PointCloudPoint &point)
{
    return glm::vec3(point.y, -point.z, point.x);
}
} // namespace

PointCloudRenderer::PointCloudRenderer(const ApplicationPaths &applicationPathsValue)
    : applicationPaths(applicationPathsValue)
{
}

PointCloudRenderer::~PointCloudRenderer()
{
    shutdown();
}

bool PointCloudRenderer::initialize()
{
    if (shaderProgram != 0)
    {
        return true;
    }

    if (!createShaderProgram())
    {
        return false;
    }

    glGenVertexArrays(1, &vertexArray);
    glGenBuffers(1, &vertexBuffer);

    glBindVertexArray(vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuPoint), reinterpret_cast<void *>(offsetof(GpuPoint, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
                          1,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(GpuPoint),
                          reinterpret_cast<void *>(offsetof(GpuPoint, intensity)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,
                          1,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(GpuPoint),
                          reinterpret_cast<void *>(offsetof(GpuPoint, distance)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void PointCloudRenderer::shutdown()
{
    if (depthRenderbuffer != 0)
    {
        glDeleteRenderbuffers(1, &depthRenderbuffer);
        depthRenderbuffer = 0;
    }

    if (colorTexture != 0)
    {
        glDeleteTextures(1, &colorTexture);
        colorTexture = 0;
    }

    if (framebuffer != 0)
    {
        glDeleteFramebuffers(1, &framebuffer);
        framebuffer = 0;
    }

    if (vertexBuffer != 0)
    {
        glDeleteBuffers(1, &vertexBuffer);
        vertexBuffer = 0;
    }

    if (vertexArray != 0)
    {
        glDeleteVertexArrays(1, &vertexArray);
        vertexArray = 0;
    }

    if (shaderProgram != 0)
    {
        glDeleteProgram(shaderProgram);
        shaderProgram = 0;
    }
}

void PointCloudRenderer::render(const SharedState::PointCloudFrame &pointCloud,
                                const SharedState::UiPreferences &preferences,
                                const ImVec2 &viewportSize)
{
    if (shaderProgram == 0)
    {
        return;
    }

    const int width = std::max(1, static_cast<int>(viewportSize.x));
    const int height = std::max(1, static_cast<int>(viewportSize.y));
    if (!ensureFramebuffer(width, height))
    {
        return;
    }

    uploadPointCloudIfNeeded(pointCloud);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shaderProgram);

    const float aspectRatio = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
    const glm::mat4 projection = camera.buildProjectionMatrix(aspectRatio);
    const glm::mat4 view = camera.buildViewMatrix();
    const glm::mat4 mvp = projection * view;

    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uMvp"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(glGetUniformLocation(shaderProgram, "uPointSize"), preferences.pointSize);
    glUniform1f(glGetUniformLocation(shaderProgram, "uMaxDistance"), std::max(maxDistance, 1.0f));
    glUniform1i(glGetUniformLocation(shaderProgram, "uColorMode"),
                preferences.colorMode == SharedState::ColorMode::intensity ? 0 : 1);

    glBindVertexArray(vertexArray);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(uploadedPointCount));
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

ImTextureRef PointCloudRenderer::getTextureRef() const
{
    return ImTextureRef(static_cast<ImTextureID>(colorTexture));
}

OrbitCamera &PointCloudRenderer::getCamera()
{
    return camera;
}

const OrbitCamera &PointCloudRenderer::getCamera() const
{
    return camera;
}

void PointCloudRenderer::drawAxisOverlay(ImDrawList *drawList, const ImVec2 &origin, float length) const
{
    if (drawList == nullptr)
    {
        return;
    }

    const glm::vec3 cameraRight = camera.getRightVector();
    const glm::vec3 cameraUp = camera.getUpVector();

    const auto projectAxis = [&](const glm::vec3 &axis) {
        const float x = glm::dot(axis, cameraRight);
        const float y = glm::dot(axis, cameraUp);
        return ImVec2(origin.x + x * length, origin.y - y * length);
    };

    const ImVec2 center = origin;
    const ImVec2 xAxis = projectAxis(glm::vec3(1.0f, 0.0f, 0.0f));
    const ImVec2 yAxis = projectAxis(glm::vec3(0.0f, 1.0f, 0.0f));
    const ImVec2 zAxis = projectAxis(glm::vec3(0.0f, 0.0f, 1.0f));

    drawList->AddLine(center, xAxis, IM_COL32(255, 90, 90, 255), 2.0f);
    drawList->AddLine(center, yAxis, IM_COL32(110, 240, 110, 255), 2.0f);
    drawList->AddLine(center, zAxis, IM_COL32(110, 170, 255, 255), 2.0f);
    drawList->AddText(ImVec2(xAxis.x + 4.0f, xAxis.y - 8.0f), IM_COL32(255, 120, 120, 255), "X");
    drawList->AddText(ImVec2(yAxis.x + 4.0f, yAxis.y - 8.0f), IM_COL32(120, 255, 120, 255), "Y");
    drawList->AddText(ImVec2(zAxis.x + 4.0f, zAxis.y - 8.0f), IM_COL32(120, 180, 255, 255), "Z");
}

bool PointCloudRenderer::createShaderProgram()
{
    const std::string vertexSource = loadShaderSource("point_cloud.vert", fallbackVertexShader);
    const std::string fragmentSource = loadShaderSource("point_cloud.frag", fallbackFragmentShader);

    const unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    const unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0)
        {
            glDeleteShader(vertexShader);
        }
        if (fragmentShader != 0)
        {
            glDeleteShader(fragmentShader);
        }
        return false;
    }

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    int linkStatus = GL_FALSE;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &linkStatus);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (linkStatus != GL_TRUE)
    {
        int logLength = 0;
        glGetProgramiv(shaderProgram, GL_INFO_LOG_LENGTH, &logLength);
        std::string infoLog(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
        glGetProgramInfoLog(shaderProgram, logLength, nullptr, infoLog.data());
        std::cerr << "Failed to link point cloud shader program: " << infoLog << std::endl;
        glDeleteProgram(shaderProgram);
        shaderProgram = 0;
        return false;
    }

    return true;
}

bool PointCloudRenderer::rebuildFramebuffer(int width, int height)
{
    if (framebuffer != 0)
    {
        glDeleteFramebuffers(1, &framebuffer);
        framebuffer = 0;
    }
    if (colorTexture != 0)
    {
        glDeleteTextures(1, &colorTexture);
        colorTexture = 0;
    }
    if (depthRenderbuffer != 0)
    {
        glDeleteRenderbuffers(1, &depthRenderbuffer);
        depthRenderbuffer = 0;
    }

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    glGenTextures(1, &colorTexture);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

    glGenRenderbuffers(1, &depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRenderbuffer);

    const bool isComplete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    framebufferWidth = width;
    framebufferHeight = height;
    return isComplete;
}

bool PointCloudRenderer::ensureFramebuffer(int width, int height)
{
    if (width == framebufferWidth && height == framebufferHeight && framebuffer != 0)
    {
        return true;
    }
    return rebuildFramebuffer(width, height);
}

void PointCloudRenderer::uploadPointCloudIfNeeded(const SharedState::PointCloudFrame &pointCloud)
{
    if (pointCloud.revision == uploadedRevision)
    {
        return;
    }

    std::vector<GpuPoint> gpuPoints;
    gpuPoints.reserve(pointCloud.points.size());
    maxDistance = 1.0f;

    for (const auto &point : pointCloud.points)
    {
        const glm::vec3 viewerPoint = convertNedToViewer(point);
        const float pointDistance = glm::length(viewerPoint);
        maxDistance = std::max(maxDistance, pointDistance);
        gpuPoints.push_back(GpuPoint{
            .x = viewerPoint.x,
            .y = viewerPoint.y,
            .z = viewerPoint.z,
            .intensity = static_cast<float>(point.intensity),
            .distance = pointDistance,
        });
    }

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(gpuPoints.size() * sizeof(GpuPoint)),
                 gpuPoints.empty() ? nullptr : gpuPoints.data(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    uploadedPointCount = gpuPoints.size();
    uploadedRevision = pointCloud.revision;
}

unsigned int PointCloudRenderer::compileShader(unsigned int shaderType, const std::string &source) const
{
    const unsigned int shader = glCreateShader(shaderType);
    const char *sourcePtr = source.c_str();
    glShaderSource(shader, 1, &sourcePtr, nullptr);
    glCompileShader(shader);

    int compileStatus = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_TRUE)
    {
        return shader;
    }

    int logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string infoLog(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, infoLog.data());
    std::cerr << "Shader compile failed: " << infoLog << std::endl;
    glDeleteShader(shader);
    return 0;
}

std::string PointCloudRenderer::loadShaderSource(const char *fileName, const char *fallback) const
{
    try
    {
        return readTextFile(applicationPaths.resourcesDir / "shaders" / fileName);
    }
    catch (...)
    {
        return fallback;
    }
}

} // namespace gcs::viewer


