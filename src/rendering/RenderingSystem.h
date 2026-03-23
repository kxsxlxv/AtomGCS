#pragma once

#include <imgui.h>

struct GLFWwindow;

namespace gcs
{

    // Низкоуровневая обёртка над GLFW + OpenGL + ImGui
    class RenderingSystem
    {
    public:
        RenderingSystem() = default;
        ~RenderingSystem();

        RenderingSystem(const RenderingSystem &) = delete;
        RenderingSystem &operator=(const RenderingSystem &) = delete;
        
        bool initialize();
        void shutdown();

        [[nodiscard]] bool shouldClose() const;
        void pollEvents();
        void beginFrame();
        void endFrame();

        [[nodiscard]] GLFWwindow *getWindow() const;
        [[nodiscard]] ImGuiIO &getIo() const;
        [[nodiscard]] float getContentScale() const;
        [[nodiscard]] const char *getGlslVersion() const;

        [[nodiscard]] ImFont *getRegularFont() const;
        [[nodiscard]] ImFont *getBoldFont() const;
        [[nodiscard]] ImFont *getMaterialIconsFont() const;
        [[nodiscard]] ImFont *getAwesomeIconsFont() const;

    private:
        bool initializeWindow();
        bool initializeImGui();
        void loadFonts();
        void applyStyle();

        GLFWwindow *window = nullptr;
        float contentScale = 1.0f;
        const char *glslVersion = "#version 330 core";

        ImFont *regularFont = nullptr;
        ImFont *boldFont = nullptr;
        ImFont *materialIconsFont = nullptr;
        ImFont *awesomeIconsFont = nullptr;
    };

}