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

/**
 * @brief Default model transform applied to BVH models.
 *
 * This transform mirrors the placement used for the rasterized bunny model
 * to ensure visual consistency across rendering modes (raster vs ray tracing).
 * It is kept as a small helper since BVH models do not have a fixed origin.
 *
 * @return A model matrix with translation and uniform scale.
 */
inline glm::mat4 defaultBvhTransform() {
    glm::mat4 M(1.0f);
    M = glm::translate(M, glm::vec3(-2.0f, 1.5f, 0.0f));
    M = glm::scale(M, glm::vec3(0.5f));
    return M;
}

/**
 * @class AppState
 * @brief Centralized container for all engine runtime state.
 *
 * This class aggregates all the data that was previously spread across
 * global variables inside main.cpp. By grouping rendering resources,
 * frame timing, models, shaders, camera state, and UI state into a
 * single structure, the program becomes easier to manage and reason about.
 *
 * AppState does *not* own the window or OpenGL context; it only holds
 * rendering- and scene-related objects required across frames.
 */
class AppState {
public:
    /// Accumulation buffer used for progressive path tracing (MRT-based).
    rt::Accum accum;

    /// G-buffer textures storing world-space position, normal, and motion vectors.
    rt::GBuffer gBuffer;

    /// Per-frame matrices and motion data used for TAA / SVGF.
    rt::FrameState frame;

    /// Collection of all render parameters (GI, exposure, debug toggles, etc.).
    RenderParams params;

    /// Whether the engine is currently rendering in ray tracing mode.
    bool rayMode = true;

    /// Debug flag for showing motion vectors in the final output.
    bool showMotion = false;

    /// Fullscreen quad VAO used by the present pass.
    GLuint fsVao = 0;

    /// Path tracer shader (primary + indirect rays).
    std::unique_ptr<Shader> rtShader;

    /// Shader responsible for tone-mapping and presenting the accumulation buffer.
    std::unique_ptr<Shader> presentShader;

    /// Rasterization shader used for comparison or debug rendering.
    std::unique_ptr<Shader> rasterShader;

    /// Time between frames used for camera movement and UI animation.
    float deltaTime = 0.0f;

    /// Timestamp of the previous frame for computing deltaTime.
    float lastFrame = 0.0f;

    /// Main camera used for both raster and ray tracing paths.
    Camera camera;

    /// Ground plane model (rasterized).
    std::unique_ptr<Model> ground;

    /// Bunny model (rasterized).
    std::unique_ptr<Model> bunny;

    /// Simple sphere model (rasterized).
    std::unique_ptr<Model> sphere;

    /// Whether the BVH system is active for ray tracing.
    bool useBVH = false;

    /// Handle to the GPU-side BVH.
    BVHHandle bvh;

    /// Node and triangle counts, displayed in the UI.
    int bvhNodeCount = 0, bvhTriCount = 0;

    /// Transform applied to the BVH geometry before intersection tests.
    glm::mat4 bvhTransform = defaultBvhTransform();

    /// Raster version of the BVH geometry, used for debugging.
    std::unique_ptr<Model> bvhModel;

    /// UI state for selecting BVH models from disk.
    ui::BvhModelPickerState bvhPicker;

    /// Environment map texture ID (IBL).
    GLuint envMapTex = 0;

    /// UI state for browsing/selecting environment maps.
    ui::EnvMapPickerState envPicker;

    /// Input state including key presses, mouse deltas, toggles, etc.
    io::InputState input;

    /**
     * @brief Initializes the application state with a default camera setup.
     *
     * The camera is positioned behind the scene and angled slightly downward.
     * The FOV and aspect ratio match the default window size. All other
     * members use their inline initializers.
     */
    AppState()
        : camera(glm::vec3(0.0f, 2.0f, 8.0f), -90.0f, -10.0f, 60.0f, 1920.0f / 1080.0f) {
    }
};
