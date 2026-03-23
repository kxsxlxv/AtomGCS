#pragma once

#include "core/PathUtils.h"
#include "state/SharedState.h"
#include "viewer/OrbitCamera.h"
#include "viewer/OverlayRenderer.h"
#include "viewer/SceneOverlay.h"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace gcs::viewer
{

    // OpenGL рендерер облака точек
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
                    const ImVec2 &viewportSize,
                    const SceneOverlay& overlay = {});

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
        };

        struct UniformLocations
        {
            int mvp = -1;
            int view = -1;
            int pointSize = -1;
            int maxDistance = -1;
            int colorMode = -1;
            int distancePointSizing = -1;
        } uniforms;

        bool createShaderProgram();
        bool rebuildFramebuffer(int width, int height);
        bool ensureFramebuffer(int width, int height);
        void uploadPointCloudIfNeeded(const SharedState::PointCloudFrame &pointCloud);
        unsigned int compileShader(unsigned int shaderType, const std::string &source) const;
        std::string loadShaderSource(const char *fileName) const;

        ApplicationPaths applicationPaths;
        OrbitCamera camera;
        OverlayRenderer overlayRenderer;

        unsigned int shaderProgram = 0;
        unsigned int vertexArray = 0;
        unsigned int vertexBuffer = 0;
        unsigned int framebuffer = 0;
        unsigned int colorTexture = 0;
        unsigned int depthRenderbuffer = 0;

        unsigned int msFramebuffer = 0; // Multisample FBO
        unsigned int msColorTexture = 0; // GL_TEXTURE_2D_MULTISAMPLE
        unsigned int msDepthRenderbuffer = 0; // GL_RENDERBUFFER, multisample
        unsigned int msColorRenderbuffer = 0;  // GL_RENDERBUFFER, multisample

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        std::uint64_t uploadedRevision = 0;
        std::size_t uploadedPointCount = 0;
        std::size_t bufferCapacityPoints = 0;
        float maxDistance = 1.0f;

        std::vector<GpuPoint> gpuPointsBuffer;
    };

}