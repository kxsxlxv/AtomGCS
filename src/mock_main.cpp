#include "mock/MockServer.h"

#include <exception>
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        const auto config = gcs::mock::parseMockServerConfig(argc, argv);
        gcs::mock::MockServer server(config);
        return server.run();
    }
    catch (const std::exception &exception)
    {
        std::cerr << "mock_server failed: " << exception.what() << std::endl;
        return 1;
    }
}