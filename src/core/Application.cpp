#include "core/Application.h"

#include "config/AppConfig.h"
#include "network/ProtocolClient.h"
#include "ui/UISystem.h"
#include "viewer/PointCloudRenderer.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace gcs
{

    namespace
    {
        ApplicationPaths fallbackPaths()
        {
            ApplicationPaths paths;
            paths.executableDir = std::filesystem::current_path();
            paths.executableFile = paths.executableDir / "AtomGCS";
            paths.resourcesDir = paths.executableDir / "resources";
            paths.appConfigFile = paths.executableDir / "app_config.json";
            paths.missionParametersFile = paths.executableDir / "mission_params.json";
            return paths;
        }
    } // namespace

    Application::Application() = default;

    Application::~Application()
    {
        shutdown();
    }

    int Application::run()
    {
        if (!initialize())
        {
            std::cerr << "Failed to initialize AtomGCS" << std::endl;
            return -1;
        }

        mainLoop();
        return 0;
    }

    bool Application::initialize()
    {
        try
        {
            applicationPaths = buildApplicationPaths();
        }
        catch (const std::exception &exception)
        {
            applicationPaths = fallbackPaths();
            sharedState.appendLog(SharedState::LogDirection::local,
                                SharedState::LogCategory::error,
                                "PATHS",
                                std::string("Path detection fallback: ") + exception.what());
        }

        loadConfiguration();

        renderingSystem = std::make_unique<RenderingSystem>();
        if (!renderingSystem->initialize())
        {
            return false;
        }

        pointCloudRenderer = std::make_unique<viewer::PointCloudRenderer>(applicationPaths);
        if (!pointCloudRenderer->initialize())
        {
            return false;
        }

        protocolClient = std::make_unique<network::ProtocolClient>(sharedState);

        uiSystem = std::make_unique<UISystem>(*renderingSystem,
                                            sharedState,
                                            *protocolClient,
                                            *pointCloudRenderer,
                                            applicationPaths);
        return true;
    }

    void Application::shutdown()
    {
        if (protocolClient)
        {
            protocolClient->disconnect();
        }

        try 
        {
            saveConfiguration();
        } 
        catch (const std::exception& e) 
        {
            std::cerr << "Failed to save config on shutdown: " << e.what() << std::endl;
        }

        uiSystem.reset();
        pointCloudRenderer.reset();
        protocolClient.reset();

        if (renderingSystem)
        {
            renderingSystem->shutdown();
            renderingSystem.reset();
        }
    }

    void Application::mainLoop()
    {
        while (renderingSystem && !renderingSystem->shouldClose())
        {
            renderingSystem->pollEvents();
            renderingSystem->beginFrame();
            if (uiSystem)
            {
                uiSystem->render();
            }
            renderingSystem->endFrame();
        }
    }

    void Application::loadConfiguration()
    {
        config::AppConfigModel appConfig = config::defaultAppConfig();
        std::string errorMessage;
        if (!config::loadAppConfig(applicationPaths.appConfigFile, appConfig, errorMessage))
        {
            sharedState.appendLog(SharedState::LogDirection::local,
                                SharedState::LogCategory::error,
                                "CONFIG",
                                "App config load failed: " + errorMessage);
            appConfig = config::defaultAppConfig();
        }

        sharedState.setConnectionSettings(appConfig.connectionSettings);
        sharedState.setUiPreferences(appConfig.uiPreferences);

        SharedState::MissionParametersModel missionParameters;
        if (!config::loadMissionParameters(applicationPaths.missionParametersFile, missionParameters, errorMessage))
        {
            sharedState.appendLog(SharedState::LogDirection::local,
                                SharedState::LogCategory::error,
                                "MISSION",
                                "Mission config load failed: " + errorMessage);
        }
        sharedState.setMissionParameters(missionParameters);
    }

    void Application::saveConfiguration() const
    {
        if (applicationPaths.appConfigFile.empty())
        {
            return;
        }

        config::AppConfigModel appConfig;
        appConfig.connectionSettings = sharedState.getConnectionSettings();
        appConfig.uiPreferences = sharedState.getUiPreferences();

        std::string errorMessage;
        if (!config::saveAppConfig(applicationPaths.appConfigFile, appConfig, errorMessage))
        {
            std::cerr << "Failed to save app config: " << errorMessage << std::endl;
        }
    }

}