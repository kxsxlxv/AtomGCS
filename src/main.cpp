#include "core/Application.h"

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
#endif

int main()
{
    gcs::Application app;
    return app.run();
}
