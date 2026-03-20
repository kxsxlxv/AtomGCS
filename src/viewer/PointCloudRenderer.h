#pragma once

#include "core/PathUtils.h"
#include "state/SharedState.h"
#include "viewer/OrbitCamera.h"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace gcs::viewer
{

class PointCloudRenderer
{
public:
    explicit PointCloudRenderer(const ApplicationPaths &applicationPaths);
    ~PointCloudRenderer();

    PointCloudRenderer(const PointCloudRenderer &) = delete;
    PointCloudRenderer &operator=(const PointCloudRenderer &) = delete;

    bool initialize();
    void shutdown();

    void render(const SharedState::PointCloudFrame &pointCloud,
                const SharedState::UiPreferences &preferences,
                const ImVec2 &viewportSize);

    [[nodiscard]] ImTextureRef getTextureRef() const;
    [[nodiscard]] OrbitCamera &getCamera();
    [[nodiscard]] const OrbitCamera &getCamera() const;
    void drawAxisOverlay(ImDrawList *drawList, const ImVec2 &origin, float length) const;

private:
    struct GpuPoint
    {
        float x;
        float y;
        float z;
        float intensity;
        float distance;
    };

    bool createShaderProgram();
    bool rebuildFramebuffer(int width, int height);
    bool ensureFramebuffer(int width, int height);
    void uploadPointCloudIfNeeded(const SharedState::PointCloudFrame &pointCloud);
    unsigned int compileShader(unsigned int shaderType, const std::string &source) const;
    std::string loadShaderSource(const char *fileName, const char *fallback) const;

    ApplicationPaths applicationPaths;
    OrbitCamera camera;

    unsigned int shaderProgram = 0;
    unsigned int vertexArray = 0;
    unsigned int vertexBuffer = 0;
    unsigned int framebuffer = 0;
    unsigned int colorTexture = 0;
    unsigned int depthRenderbuffer = 0;

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    std::uint64_t uploadedRevision = 0;
    std::size_t uploadedPointCount = 0;
    float maxDistance = 1.0f;
};

} // namespace gcs::viewer
