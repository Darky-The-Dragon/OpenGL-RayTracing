cmake_minimum_required(VERSION 3.5)
project(OpenGLRayTracing)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------

# GLFW
add_subdirectory(libs/glfw)
include_directories(libs/glfw/include)

# GLAD 2
add_library(glad STATIC libs/glad/src/gl.c)
target_include_directories(glad PUBLIC libs/glad/include)

# ImGui
file(GLOB IMGUI_SOURCES libs/imgui/*.cpp)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC libs/imgui)

# GLM (header-only)
include_directories(libs/glm)

# TinyObjLoader (header-only)
include_directories(libs/tinyobjloader)

# Assimp from submodule
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Disable tests")
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "Disable tools")
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "Disable samples")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static build")
set(ASSIMP_NO_EXPORT ON CACHE BOOL "Disable export")
set(ASSIMP_INSTALL OFF CACHE BOOL "No install")

add_subdirectory(libs/assimp)
include_directories(libs/assimp/include)

# ------------------------------------------------------------
# Project Sources
# ------------------------------------------------------------

# Add public and local headers
include_directories(
        include
        include/camera
        include/utils
        include/io
        src
        src/io
        src/rt
)

file(GLOB_RECURSE SRC_FILES src/*.cpp)

add_executable(OpenGLRayTracing ${SRC_FILES})

# ------------------------------------------------------------
# Linking
# ------------------------------------------------------------

target_link_libraries(OpenGLRayTracing glfw glad imgui assimp)

if (APPLE)
    target_link_libraries(OpenGLRayTracing
            "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif ()

if ((WIN32 OR UNIX) AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(OpenGLRayTracing OpenGL::GL)
endif ()