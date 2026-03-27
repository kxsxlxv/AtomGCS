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

        constexpr int initialWidth = 1920;
        constexpr int initialHeight = 1080;
        window = glfwCreateWindow(initialWidth, initialHeight, "AtomGCS", nullptr, nullptr);
        if (window == nullptr)
        {
            return false;
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1); // VSync ON
        
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
                                                            26.0f * contentScale,
                                                            &iconConfig);
    }

    void RenderingSystem::applyStyle()
    {
        ImGui::StyleColorsDark();

        ImGuiStyle &style = ImGui::GetStyle();

        // Geometry — use float literals, ScaleAllSizes will handle DPI
        style.WindowRounding   = 0.0f;
        style.ChildRounding    = 2.0f;
        style.FrameRounding    = 2.0f;
        style.PopupRounding    = 2.0f;
        style.ScrollbarRounding= 2.0f;
        style.GrabRounding     = 2.0f;
        style.TabRounding      = 2.0f;
        style.ImageRounding    = 2.0f;

        style.WindowPadding    = ImVec2(10, 10);
        style.FramePadding     = ImVec2(8, 4);
        style.ItemSpacing      = ImVec2(8, 6);
        style.ItemInnerSpacing = ImVec2(6, 4);
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize  = 1.0f;
        style.ChildBorderSize  = 1.0f;
        style.SeparatorSize    = 1.0f;

        // Palette — blue accent on dark background
        ImVec4 *c = style.Colors;

        // Core backgrounds
        c[ImGuiCol_WindowBg]           = ImVec4(0.102f, 0.114f, 0.137f, 1.000f); // #1A1D23
        //c[ImGuiCol_ChildBg]            = ImVec4(0.118f, 0.129f, 0.157f, 1.000f); // #1E2128
        c[ImGuiCol_PopupBg]            = ImVec4(0.118f, 0.129f, 0.157f, 0.960f);

        // Borders
        c[ImGuiCol_Border]             = ImVec4(0.208f, 0.220f, 0.251f, 1.000f); // #353840
        c[ImGuiCol_BorderShadow]       = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);

        // Frame backgrounds (inputs, sliders, etc.)
        c[ImGuiCol_FrameBg]            = ImVec4(0.145f, 0.157f, 0.188f, 1.000f); // #252830
        c[ImGuiCol_FrameBgHovered]     = ImVec4(0.180f, 0.192f, 0.224f, 1.000f);
        c[ImGuiCol_FrameBgActive]      = ImVec4(0.212f, 0.224f, 0.255f, 1.000f);

        // Title bar
        c[ImGuiCol_TitleBg]            = ImVec4(0.082f, 0.094f, 0.125f, 1.000f); // #151820
        c[ImGuiCol_TitleBgActive]      = ImVec4(0.102f, 0.114f, 0.137f, 1.000f); // #1A1D23
        c[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.082f, 0.094f, 0.125f, 0.750f);
        c[ImGuiCol_MenuBarBg]          = ImVec4(0.102f, 0.114f, 0.137f, 1.000f);

        // Scrollbar
        c[ImGuiCol_ScrollbarBg]        = ImVec4(0.102f, 0.114f, 0.137f, 0.600f);
        c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.208f, 0.220f, 0.251f, 1.000f);
        c[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.290f, 0.561f, 0.851f, 0.700f); // blue tint
        c[ImGuiCol_ScrollbarGrabActive]= ImVec4(0.290f, 0.561f, 0.851f, 1.000f);

        // Accent blue: #4A90D9
        c[ImGuiCol_CheckMark]          = ImVec4(0.290f, 0.565f, 0.851f, 1.000f);
        c[ImGuiCol_SliderGrab]         = ImVec4(0.290f, 0.565f, 0.851f, 0.800f);
        c[ImGuiCol_SliderGrabActive]   = ImVec4(0.353f, 0.627f, 0.914f, 1.000f); // #5BA0E9

        // Buttons
        c[ImGuiCol_Button]             = ImVec4(0.208f, 0.220f, 0.251f, 1.000f);
        c[ImGuiCol_ButtonHovered]      = ImVec4(0.290f, 0.565f, 0.851f, 0.600f);
        c[ImGuiCol_ButtonActive]       = ImVec4(0.290f, 0.565f, 0.851f, 1.000f);

        // Headers (collapsing header, tree node)
        c[ImGuiCol_Header]             = ImVec4(0.208f, 0.220f, 0.251f, 1.000f);
        c[ImGuiCol_HeaderHovered]      = ImVec4(0.290f, 0.565f, 0.851f, 0.400f);
        c[ImGuiCol_HeaderActive]       = ImVec4(0.290f, 0.565f, 0.851f, 0.700f);

        // Separators
        c[ImGuiCol_Separator]          = ImVec4(0.208f, 0.220f, 0.251f, 1.000f);
        c[ImGuiCol_SeparatorHovered]   = ImVec4(0.290f, 0.565f, 0.851f, 0.700f);
        c[ImGuiCol_SeparatorActive]    = ImVec4(0.290f, 0.565f, 0.851f, 1.000f);

        // Resize grip
        c[ImGuiCol_ResizeGrip]         = ImVec4(0.208f, 0.220f, 0.251f, 0.500f);
        c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.290f, 0.565f, 0.851f, 0.600f);
        c[ImGuiCol_ResizeGripActive]   = ImVec4(0.290f, 0.565f, 0.851f, 0.900f);

        // Tabs
        c[ImGuiCol_Tab]                = ImVec4(0.145f, 0.157f, 0.188f, 1.000f);
        c[ImGuiCol_TabHovered]         = ImVec4(0.290f, 0.565f, 0.851f, 0.500f);
        c[ImGuiCol_TabSelected]        = ImVec4(0.224f, 0.435f, 0.678f, 1.000f); // darker blue
        c[ImGuiCol_TabSelectedOverline]= ImVec4(0.290f, 0.565f, 0.851f, 1.000f);
        c[ImGuiCol_TabDimmed]          = ImVec4(0.102f, 0.114f, 0.137f, 1.000f);
        c[ImGuiCol_TabDimmedSelected]  = ImVec4(0.176f, 0.353f, 0.549f, 1.000f);
        c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.208f, 0.220f, 0.251f, 1.000f);

        // Docking
        c[ImGuiCol_DockingPreview]     = ImVec4(0.290f, 0.565f, 0.851f, 0.500f);
        c[ImGuiCol_DockingEmptyBg]     = ImVec4(0.082f, 0.094f, 0.125f, 1.000f);

        // Plot colors
        c[ImGuiCol_PlotLines]          = ImVec4(0.290f, 0.565f, 0.851f, 1.000f);
        c[ImGuiCol_PlotLinesHovered]   = ImVec4(0.353f, 0.627f, 0.914f, 1.000f);
        c[ImGuiCol_PlotHistogram]      = ImVec4(0.290f, 0.565f, 0.851f, 1.000f);
        c[ImGuiCol_PlotHistogramHovered]= ImVec4(0.353f, 0.627f, 0.914f, 1.000f);

        // Tables
        c[ImGuiCol_TableHeaderBg]      = ImVec4(0.145f, 0.157f, 0.188f, 1.000f);
        c[ImGuiCol_TableBorderStrong]  = ImVec4(0.208f, 0.220f, 0.251f, 1.000f);
        c[ImGuiCol_TableBorderLight]   = ImVec4(0.208f, 0.220f, 0.251f, 0.600f);
        c[ImGuiCol_TableRowBg]         = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);
        c[ImGuiCol_TableRowBgAlt]      = ImVec4(1.000f, 1.000f, 1.000f, 0.020f);

        // Text
        c[ImGuiCol_Text]               = ImVec4(0.878f, 0.878f, 0.878f, 1.000f); // #E0E0E0
        c[ImGuiCol_TextDisabled]       = ImVec4(0.439f, 0.439f, 0.439f, 1.000f); // #707070
        c[ImGuiCol_TextSelectedBg]     = ImVec4(0.290f, 0.565f, 0.851f, 0.350f);
        c[ImGuiCol_TextLink]           = ImVec4(0.290f, 0.565f, 0.851f, 1.000f);

        // Misc
        c[ImGuiCol_DragDropTarget]     = ImVec4(0.290f, 0.565f, 0.851f, 0.900f);
        c[ImGuiCol_NavCursor]          = ImVec4(0.290f, 0.565f, 0.851f, 1.000f);
        c[ImGuiCol_ModalWindowDimBg]   = ImVec4(0.000f, 0.000f, 0.000f, 0.600f);

        // Scale everything for DPI
        style.ScaleAllSizes(contentScale);

        if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
    }

}