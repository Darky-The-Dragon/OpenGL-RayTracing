cmake_minimum_required(VERSION 3.1)
project(OpenGLRayTracing)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------

# GLFW
add_subdirectory(libs/glfw)
include_directories(libs/glfw/include)

# GLAD (GLAD 2 uses gl.c and gl.h)
add_library(glad STATIC libs/glad/src/gl.c)
target_include_directories(glad PUBLIC libs/glad/include)

# ImGui
file(GLOB IMGUI_SOURCES libs/imgui/*.cpp)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC libs/imgui)

# GLM (header-only, no build step)
include_directories(libs/glm)

# ------------------------------------------------------------
# Project Source Files
# ------------------------------------------------------------

# Your own project headers
include_directories(include)

# Automatically collect all .cpp files in src/
file(GLOB_RECURSE SRC_FILES src/*.cpp)

# Main executable
add_executable(OpenGLRayTracing
        ${SRC_FILES}
        src/Camera.cpp
        src/Camera.h
        src/Shader.cpp
        src/Shader.h
)

# ------------------------------------------------------------
# Linking
# ------------------------------------------------------------

# Link core libraries
target_link_libraries(OpenGLRayTracing glfw glad imgui)

# macOS-specific OpenGL frameworks
if(APPLE)
    target_link_libraries(OpenGLRayTracing "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

# Windows/Linux-specific OpenGL linking
if(WIN32 OR UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(OpenGLRayTracing OpenGL::GL)
endif()
