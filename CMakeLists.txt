cmake_minimum_required(VERSION 3.20)
project(AtomGCS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

enable_testing()

# ==================== GLFW ====================
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

# ==================== Dear ImGui ====================
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${SOURCE_DIR}/rendering/ImGuiOpenGlBackend.cpp
)

target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_CUSTOM GLFW_INCLUDE_NONE)

target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/deps
)

# ==================== AtomGCS Core ====================
file(GLOB_RECURSE ATOMGCS_CORE_SOURCES CONFIGURE_DEPENDS
    "${SOURCE_DIR}/*.cpp"
    "${SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/shared/**/*.h"
)

list(FILTER ATOMGCS_CORE_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
list(FILTER ATOMGCS_CORE_SOURCES EXCLUDE REGEX ".*/mock_main\\.cpp$")
list(FILTER ATOMGCS_CORE_SOURCES EXCLUDE REGEX ".*/mavlink/.*")
list(FILTER ATOMGCS_CORE_SOURCES EXCLUDE REGEX ".*/data/DroneData\\.h$")
list(FILTER ATOMGCS_CORE_SOURCES EXCLUDE REGEX ".*/vc140\\.pdb$")
list(FILTER ATOMGCS_CORE_SOURCES EXCLUDE REGEX ".*/ImGuiOpenGlBackend\\.cpp$")

add_library(atomgcs_core STATIC ${ATOMGCS_CORE_SOURCES})

target_include_directories(atomgcs_core PUBLIC
    ${SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CMAKE_CURRENT_SOURCE_DIR}/external/asio/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/deps
)

target_compile_definitions(atomgcs_core PUBLIC
    ASIO_STANDALONE
    IMGUI_IMPL_OPENGL_LOADER_CUSTOM
    GLFW_INCLUDE_NONE
)

if(WIN32)
    target_compile_definitions(atomgcs_core PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601
    )
endif()

target_link_libraries(atomgcs_core PUBLIC
    glfw
    imgui
    Threads::Threads
    OpenGL::GL
)

if(WIN32)
    target_link_libraries(atomgcs_core PUBLIC ws2_32)
endif()

if(MSVC)
    target_compile_options(atomgcs_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(atomgcs_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(${PROJECT_NAME} ${SOURCE_DIR}/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE atomgcs_core)

add_executable(mock_server ${SOURCE_DIR}/mock_main.cpp)
target_link_libraries(mock_server PRIVATE atomgcs_core)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources"
    COMMENT "Copying runtime resources"
)

add_executable(atomgcs_smoke_tests tests/SmokeTests.cpp)
target_link_libraries(atomgcs_smoke_tests PRIVATE atomgcs_core)

add_test(NAME atomgcs_smoke_tests COMMAND atomgcs_smoke_tests)

add_executable(mock_server_integration_tests tests/MockServerIntegrationTests.cpp)
target_link_libraries(mock_server_integration_tests PRIVATE atomgcs_core)
add_test(NAME mock_server_integration_tests COMMAND mock_server_integration_tests)