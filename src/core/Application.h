#pragma once

#include "core/PathUtils.h"
#include "rendering/RenderingSystem.h"
#include "state/SharedState.h"

#include <memory>

namespace gcs
{

    namespace network
    {
        class ProtocolClient;
    }

    namespace viewer
    {
        class PointCloudRenderer;
    }

    class UISystem;

    class Application
    {
    public:
        Application();
        ~Application();

        int run();

    private:
        bool initialize();
        void shutdown();
        void mainLoop();
        void loadConfiguration();
        void saveConfiguration() const;

        ApplicationPaths applicationPaths;
        SharedState sharedState;
        std::unique_ptr<RenderingSystem> renderingSystem;
        std::unique_ptr<network::ProtocolClient> protocolClient;
        std::unique_ptr<viewer::PointCloudRenderer> pointCloudRenderer;
        std::unique_ptr<UISystem> uiSystem;
    };

} 