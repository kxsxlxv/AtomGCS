#pragma once

#include <glad/gl.h>

struct GLFWwindow;

namespace gcs
{

    bool initializeOpenGlLoader(GLFWwindow *window);

}