#pragma once

#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "render/accum.h"
#include "render/gbuffer.h"
#include "render/frame_state.h"
#include "render/RenderParams.h"
#include "render/Shader.h"
#include "scene/model.h"
#include "scene/bvh.h"
#include "io/input.h"
#include "ui/gui.h"
#include "io/Camera.h"

// Shared default transform for BVH models (matches raster bunny placement)
inline glm::mat4 defaultBvhTransform() {
    glm::mat4 M(1.0f);
    M = glm::translate(M, glm::vec3(-2.0f, 1.5f, 0.0f));
    M = glm::scale(M, glm::vec3(0.5f));
    return M;
}

// Centralized state that used to live as globals in main.cpp
struct AppState {
    rt::Accum accum;
    rt::GBuffer gBuffer;
    rt::FrameState frame;
    RenderParams params;
    RenderParamsUBO paramsUBO;
    bool rayMode = true;
    bool showMotion = false;

    GLuint fsVao = 0;
    std::unique_ptr<Shader> rtShader;
    std::unique_ptr<Shader> presentShader;
    std::unique_ptr<Shader> rasterShader;

    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    Camera camera;

    std::unique_ptr<Model> ground;
    std::unique_ptr<Model> bunny;
    std::unique_ptr<Model> sphere;

    bool useBVH = false;
    BVHHandle bvh;
    int bvhNodeCount = 0, bvhTriCount = 0;
    glm::mat4 bvhTransform = defaultBvhTransform();
    std::unique_ptr<Model> bvhModel;
    ui::BvhModelPickerState bvhPicker;

    io::InputState input;

    AppState()
        : camera(glm::vec3(0.0f, 2.0f, 8.0f), -90.0f, -10.0f, 60.0f, 1920.0f / 1080.0f) {}
};
