#define GLAD_GL_IMPLEMENTATION
#include "graphics/GlLoader.h"

#include <GLFW/glfw3.h>

namespace gcs
{

    bool initializeOpenGlLoader(GLFWwindow *window)
    {
        return window != nullptr && gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)) != 0;
    }

}
