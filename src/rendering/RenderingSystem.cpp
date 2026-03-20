#include "rendering/RenderingSystem.h"

#include "fonts/Font-Awesome-7-Free-Solid-900.h"
#include "fonts/IconsMaterialSymbolsOutlined.h"
#include "fonts/Roboto-Bold.h"
#include "fonts/Roboto-Regular.h"
#include "graphics/GlLoader.h"

#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <iostream>

namespace gcs
{

    namespace
    {
        void glfwErrorCallback(int errorCode, const char *description)
        {
            std::cerr << "GLFW error " << errorCode << ": " << description << std::endl;
        }
    }

    RenderingSystem::~RenderingSystem()
    {
        shutdown();
    }

    bool RenderingSystem::initialize()
    {
        if (window != nullptr)
        {
            return true;
        }

        glfwSetErrorCallback(glfwErrorCallback);
        if (!glfwInit())
        {
            return false;
        }

        if (!initializeWindow())
        {
            shutdown();
            return false;
        }

        if (!initializeImGui())
        {
            shutdown();
            return false;
        }

        return true;
    }

    void RenderingSystem::shutdown()
    {
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }

        if (window != nullptr)
        {
            glfwDestroyWindow(window);
            window = nullptr;
        }

        glfwTerminate();
    }

    bool RenderingSystem::shouldClose() const
    {
        return window == nullptr || glfwWindowShouldClose(window) != 0;
    }

    void RenderingSystem::pollEvents()
    {
        glfwPollEvents();
    }

    void RenderingSystem::beginFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void RenderingSystem::endFrame()
    {
        ImGui::Render();

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            GLFWwindow *backupWindow = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backupWindow);
        }

        glfwSwapBuffers(window);
    }

    GLFWwindow *RenderingSystem::getWindow() const
    {
        return window;
    }

    ImGuiIO &RenderingSystem::getIo() const
    {
        return ImGui::GetIO();
    }

    float RenderingSystem::getContentScale() const
    {
        return contentScale;
    }

    const char *RenderingSystem::getGlslVersion() const
    {
        return glslVersion;
    }

    ImFont *RenderingSystem::getRegularFont() const
    {
        return regularFont;
    }

    ImFont *RenderingSystem::getBoldFont() const
    {
        return boldFont;
    }

    ImFont *RenderingSystem::getMaterialIconsFont() const
    {
        return materialIconsFont;
    }

    ImFont *RenderingSystem::getAwesomeIconsFont() const
    {
        return awesomeIconsFont;
    }

    bool RenderingSystem::initializeWindow()
    {
    #if defined(__APPLE__)
        glslVersion = "#version 150";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #else
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #endif

        glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
        glfwWindowHint(GLFW_DEPTH_BITS, 24);
        glfwWindowHint(GLFW_STENCIL_BITS, 8);

        glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE); //FullScreen Mode

        GLFWmonitor *primaryMonitor = glfwGetPrimaryMonitor();
        float xScale = 1.0f;
        float yScale = 1.0f;
        if (primaryMonitor != nullptr)
        {
            glfwGetMonitorContentScale(primaryMonitor, &xScale, &yScale);
        }
        contentScale = xScale;

        constexpr int initialWidth = 1600;
        constexpr int initialHeight = 950;
        window = glfwCreateWindow(initialWidth, initialHeight, "AtomGCS", nullptr, nullptr);
        if (window == nullptr)
        {
            return false;
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);

        if (!initializeOpenGlLoader(window))
        {
            return false;
        }

        glEnable(GL_PROGRAM_POINT_SIZE);
        glEnable(GL_DEPTH_TEST);
        return true;
    }

    bool RenderingSystem::initializeImGui()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.ConfigWindowsMoveFromTitleBarOnly = true;

        if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
        {
            return false;
        }

        if (!ImGui_ImplOpenGL3_Init(glslVersion))
        {
            return false;
        }

        loadFonts();
        applyStyle();
        return true;
    }

    void RenderingSystem::loadFonts()
    {
        ImGuiIO &io = ImGui::GetIO();
        io.Fonts->Clear();

        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = true;
        fontConfig.FontDataOwnedByAtlas = false;

        const float baseSize = 18.0f * contentScale;
        regularFont = io.Fonts->AddFontFromMemoryTTF(roboto_regular, sizeof(roboto_regular), baseSize, &fontConfig);
        boldFont = io.Fonts->AddFontFromMemoryTTF(roboto_bold, sizeof(roboto_bold), baseSize, &fontConfig);
        io.FontDefault = regularFont;

        ImFontConfig iconConfig;
        iconConfig.OversampleH = 2;
        iconConfig.OversampleV = 2;
        iconConfig.PixelSnapH = true;
        iconConfig.FontDataOwnedByAtlas = false;

        awesomeIconsFont = io.Fonts->AddFontFromMemoryTTF(font_awesome_7_free_solid,
                                                        sizeof(font_awesome_7_free_solid),
                                                        26.0f * contentScale,
                                                        &iconConfig);
        materialIconsFont = io.Fonts->AddFontFromMemoryTTF(materialSymbolsOutlined,
                                                            sizeof(materialSymbolsOutlined),
                                                            24.0f * contentScale,
                                                            &iconConfig);
    }

    void RenderingSystem::applyStyle()
    {
        ImGui::StyleColorsDark();

        ImGuiStyle &style = ImGui::GetStyle();
        style.ScaleAllSizes(contentScale);
        // style.WindowRounding = 8.0f;
        // style.FrameRounding = 6.0f;
        // style.TabRounding = 6.0f;
        // style.ScrollbarRounding = 8.0f;
        // style.FrameBorderSize = 1.0f;
        // style.WindowBorderSize = 1.0f;

        if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            // style.WindowRounding = 6.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
    }

}