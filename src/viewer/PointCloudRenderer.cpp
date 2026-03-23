#include "viewer/PointCloudRenderer.h"

#include "graphics/GlLoader.h"

#include <glad/gl.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace gcs::viewer
{

    namespace
    {
        glm::vec3 convertNedToEnu(const protocol::PointCloudPoint &point)
        {
            return glm::vec3(point.y, point.x, -point.z);
        }

        glm::vec3 convertEnuToViewer(const glm::vec3 &enuPoint)
        {
            return glm::vec3(enuPoint.x, enuPoint.z, enuPoint.y);
        }

        glm::vec3 convertNedToViewer(const protocol::PointCloudPoint &point)
        {
            return convertEnuToViewer(convertNedToEnu(point));
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

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        if (!overlayRenderer.initialize()) 
        {
            return false;
        }

        return true;
    }

    void PointCloudRenderer::shutdown()
    {
        overlayRenderer.shutdown();

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
                                    const ImVec2 &viewportSize,
                                    const SceneOverlay& overlay)
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

        // Рендер в multisample FBO
        glBindFramebuffer(GL_FRAMEBUFFER, msFramebuffer);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        const float aspectRatio = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
        const glm::mat4 projection = camera.buildProjectionMatrix(aspectRatio);
        const glm::mat4 view = camera.buildViewMatrix();
        const glm::mat4 mvp = projection * view;

        glUniformMatrix4fv(uniforms.mvp, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(uniforms.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniform1f(uniforms.pointSize, preferences.pointSize);
        glUniform1f(uniforms.maxDistance, std::max(maxDistance, 1.0f));
        glUniform1i(uniforms.colorMode, preferences.colorMode == SharedState::ColorMode::intensity ? 0 : 1);
        glUniform1i(uniforms.distancePointSizing, preferences.distancePointSizing ? 1 : 0);

        glBindVertexArray(vertexArray);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(uploadedPointCount));
        glBindVertexArray(0);

        // Рендер overlay (линии, кубы)
        if (!overlay.empty())
        {
            overlayRenderer.render(overlay, mvp, camera.getPosition());
        }

        // Resolve multisample → обычный FBO
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glBlitFramebuffer(0, 0, framebufferWidth, framebufferHeight,
                        0, 0, framebufferWidth, framebufferHeight,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
        // Depth buffer не копируем — он не нужен для ImGui

        // Восстановление состояния
        glDisable(GL_BLEND);
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
        const std::string vertexSource = loadShaderSource("point_cloud.vert");
        const std::string fragmentSource = loadShaderSource("point_cloud.frag");

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

        uniforms.mvp = glGetUniformLocation(shaderProgram, "uMvp");
        uniforms.view = glGetUniformLocation(shaderProgram, "uView");
        uniforms.pointSize = glGetUniformLocation(shaderProgram, "uPointSize");
        uniforms.maxDistance = glGetUniformLocation(shaderProgram, "uMaxDistance");
        uniforms.colorMode = glGetUniformLocation(shaderProgram, "uColorMode");
        uniforms.distancePointSizing = glGetUniformLocation(shaderProgram, "uDistancePointSizing");

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
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRenderbuffer);

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

        // Удалить старые объекты (если есть)
        if (msFramebuffer != 0) glDeleteFramebuffers(1, &msFramebuffer);
        if (msColorRenderbuffer != 0) glDeleteRenderbuffers(1, &msColorRenderbuffer);
        if (msDepthRenderbuffer != 0) glDeleteRenderbuffers(1, &msDepthRenderbuffer);
        if (framebuffer != 0) glDeleteFramebuffers(1, &framebuffer);
        if (colorTexture != 0) glDeleteTextures(1, &colorTexture);
        if (depthRenderbuffer != 0) glDeleteRenderbuffers(1, &depthRenderbuffer);

        // Создать multisample FBO (4x MSAA)
        glGenFramebuffers(1, &msFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, msFramebuffer);

        // Color buffer (multisample)
        glGenRenderbuffers(1, &msColorRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, msColorRenderbuffer);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColorRenderbuffer);

        // Depth buffer (multisample)
        glGenRenderbuffers(1, &msDepthRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, msDepthRenderbuffer);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepthRenderbuffer);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            std::cerr << "Multisample FBO incomplete!" << std::endl;
            return false;
        }

        // Создать resolve FBO (обычный, для ImGui)
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

        // Color texture (обычная, для ImGui::Image)
        glGenTextures(1, &colorTexture);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

        // Depth buffer (не нужен в resolve FBO, только в multisample)
        glGenRenderbuffers(1, &depthRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRenderbuffer);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            std::cerr << "Resolve FBO incomplete!" << std::endl;
            return false;
        }

        framebufferWidth = width;
        framebufferHeight = height;
    }

    void PointCloudRenderer::uploadPointCloudIfNeeded(const SharedState::PointCloudFrame &pointCloud)
    {
        if (pointCloud.revision == uploadedRevision)
        {
            return;
        }

        gpuPointsBuffer.clear();  // clear() не освобождает capacity
        gpuPointsBuffer.reserve(pointCloud.points.size());
        maxDistance = 1.0f;

        for (const auto &point : pointCloud.points)
        {
            const glm::vec3 viewerPoint = convertNedToViewer(point);
            maxDistance = std::max(maxDistance, glm::length(viewerPoint));
            gpuPointsBuffer.push_back(GpuPoint{
                .x = viewerPoint.x,
                .y = viewerPoint.y,
                .z = viewerPoint.z,
                .intensity = static_cast<float>(point.intensity),
            });
        }

        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);

        if (gpuPointsBuffer.size() > bufferCapacityPoints)
        {
            bufferCapacityPoints = std::max<std::size_t>(gpuPointsBuffer.size(), bufferCapacityPoints == 0 ? 1024 : bufferCapacityPoints * 2);
            glBufferData(GL_ARRAY_BUFFER,
                        static_cast<GLsizeiptr>(bufferCapacityPoints * sizeof(GpuPoint)),
                        nullptr,
                        GL_DYNAMIC_DRAW);
        }

        if (!gpuPointsBuffer.empty())
        {
            glBufferSubData(GL_ARRAY_BUFFER,
                            0,
                            static_cast<GLsizeiptr>(gpuPointsBuffer.size() * sizeof(GpuPoint)),
                            gpuPointsBuffer.data());
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        uploadedPointCount = gpuPointsBuffer.size();
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

    std::string PointCloudRenderer::loadShaderSource(const char *fileName) const
    {
        return readTextFile(applicationPaths.resourcesDir / "shaders" / fileName);
    }

}