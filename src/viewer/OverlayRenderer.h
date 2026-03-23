#pragma once

#include "viewer/SceneOverlay.h"

#include <glm/mat4x4.hpp>
#include <vector>

namespace gcs::viewer
{

    class OverlayRenderer
    {
    public:
        OverlayRenderer() = default;
        ~OverlayRenderer();

        OverlayRenderer(const OverlayRenderer&) = delete;
        OverlayRenderer& operator=(const OverlayRenderer&) = delete;

        bool initialize();
        void shutdown();

        /// Рендерит все overlay-объекты. Вызывать ПОСЛЕ рендеринга облака точек, пока FBO активен.
        void render(const SceneOverlay& overlay, 
                    const glm::mat4& mvp,
                    const glm::vec3& cameraPos);

    private:
        // GPU-вершина: позиция + цвет
        struct Vertex
        {
            float x, y, z;
            float r, g, b, a;
        };

        bool createShaderProgram();
        
        void renderLines(const SceneOverlay& overlay, const glm::mat4& mvp);
        void renderBoxEdges(const SceneOverlay& overlay, const glm::mat4& mvp);
        void renderBoxFaces(const SceneOverlay& overlay, 
                           const glm::mat4& mvp,
                           const glm::vec3& cameraPos);
        void renderArrows(const SceneOverlay& overlay, const glm::mat4& mvp);

        // Генерация геометрии
        void buildBoxEdgeVertices(const Box& box, std::vector<Vertex>& out);
        void buildBoxFaceVertices(const Box& box, std::vector<Vertex>& out);
        void buildArrowVertices(const Arrow& arrow, std::vector<Vertex>& out);

        void uploadAndDraw(const std::vector<Vertex>& vertices, 
                          unsigned int mode,  // GL_LINES или GL_TRIANGLES
                          float lineWidth = 1.0f);

        unsigned int shaderProgram = 0;
        unsigned int vao = 0;
        unsigned int vbo = 0;
        int uniformMvp = -1;

        std::vector<Vertex> vertexBuffer;  // Переиспользуемый буфер
    };

}