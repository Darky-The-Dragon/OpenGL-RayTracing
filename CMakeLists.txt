cmake_minimum_required(VERSION 3.1)
project(OpenGLRayTracing)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GLFW
add_subdirectory(libs/glfw)
include_directories(libs/glfw/include)

# GLAD 2 (named gl.c/gl.h)
add_library(glad STATIC libs/glad/src/gl.c)
target_include_directories(glad PUBLIC libs/glad/include)

# ImGui
file(GLOB IMGUI_SOURCES libs/imgui/*.cpp)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC libs/imgui)

# Your project includes
include_directories(include)

# Source files
file(GLOB_RECURSE SRC_FILES src/*.cpp)

# Main executable
add_executable(OpenGLRayTracing ${SRC_FILES})

# Link libraries
target_link_libraries(OpenGLRayTracing glfw glad imgui)

# macOS specific frameworks
if(APPLE)
    target_link_libraries(OpenGLRayTracing "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

# Windows/Linux: link OpenGL normally
if(WIN32 OR UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(OpenGLRayTracing OpenGL::GL)
endif()
