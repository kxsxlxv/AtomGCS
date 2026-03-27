#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

namespace gcs::viewer
{

    struct LineSegment
    {
        glm::vec3 start;
        glm::vec3 end;
        glm::vec4 color;        // RGBA, A = 1.0 для непрозрачных
        float thickness = 2.0f; // Толщина линии в пикселях
    };

    // Вектор = линия + 3D конус (наконечник)
    struct Arrow
    {
        glm::vec3 origin;
        glm::vec3 direction;        // Не обязательно нормализован, длина = длина стрелки
        glm::vec4 color;
        float thickness = 2.0f;     // Толщина стержня в пикселях
        float headSize = 0.3f;      // Размер наконечника относительно длины
        int headSegments = 64;      // Сегментов в конусе (больше = плавнее)
    };

    struct Box
    {
        glm::vec3 center;
        glm::vec3 halfExtents;  // Половина размера по каждой оси
        glm::vec4 faceColor;    // RGBA, A < 1.0 для полупрозрачности
        glm::vec4 edgeColor;    // Цвет рёбер (обычно ярче, A = 1.0)
        float edgeThickness = 1.5f;
    };

    // Всё что нужно отрисовать поверх облака точек
    struct SceneOverlay
    {
        std::vector<LineSegment> lines;
        std::vector<Arrow> arrows;
        std::vector<Box> boxes;

        void clear()
        {
            lines.clear();
            arrows.clear();
            boxes.clear();
        }

        [[nodiscard]] bool empty() const
        {
            return lines.empty() && arrows.empty() && boxes.empty();
        }
    };

}