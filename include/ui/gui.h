#pragma once

#include <GLFW/glfw3.h>
#include "render/RenderParams.h"
#include "render/frame_state.h"
#include "io/input.h"

/// ImGui user interface layer: control panels, pickers, HUD elements, and debug console.
namespace ui {
    /**
     * @brief Initializes the UI system (Dear ImGui).
     *
     * Must be called exactly once after the OpenGL context and GLFW window
     * have been created. Sets up ImGui backend bindings for GLFW and OpenGL.
     *
     * @param window Pointer to the GLFW window used for input and rendering.
     */
    void Init(GLFWwindow *window);

    /**
     * @brief Shuts down all UI-related resources.
     *
     * Called once during application shutdown. Frees ImGui contexts and
     * backend bindings.
     */
    void Shutdown();

    /**
     * @brief Begins a new UI frame.
     *
     * Called at the **start of each frame**, before rendering the main scene.
     * Initializes ImGui's internal state for that frame.
     */
    void BeginFrame();

    /**
     * @brief Finalizes the UI frame and renders the UI draw data.
     *
     * Called **after** the main scene has been rendered to the back buffer.
     * Issues all ImGui draw calls.
     */
    void EndFrame();

    /**
     * @struct BvhModelPickerState
     * @brief Stores UI state for selecting BVH models.
     *
     * Tracks the current file path, selected index in the dropdown, and
     * a flag requesting a BVH reload. Used by the control panel when BVH
     * mode is enabled.
     */
    struct BvhModelPickerState {
        bool reloadRequested = false; ///< True if the user requested to reload the BVH model.
        int selectedIndex = 0; ///< Index of the model selected in the UI dropdown.
        char currentPath[256] = "../models/bunny_lp.obj"; ///< Current path to the BVH model file.
    };

    /**
     * @struct EnvMapPickerState
     * @brief Stores UI state for selecting environment maps.
     *
     * Mirrors the BVH model picker but for environment lighting.
     */
    struct EnvMapPickerState {
        bool reloadRequested = false; ///< True if an environment map reload was requested.
        int selectedIndex = 0; ///< Index of the selected environment map.
        char currentPath[256] = "../cubemaps/Sky_16.png"; ///< Current HDR/PNG cubemap path.
    };

    /**
     * @brief Draws the application's control panel.
     *
     * This UI exposes:
     *  - Material and lighting parameters (RenderParams)
     *  - Toggling between raster and ray tracing modes
     *  - BVH usage toggle and BVH model picker
     *  - Motion-vector visualization options
     *  - Debug information from InputState and FrameState
     *
     * @param params      Render parameters to modify.
     * @param frame       Current frame state for debug visualization.
     * @param input       Input state (read-only).
     * @param rayMode     Toggle between raster and ray/path tracing.
     * @param useBVH      Toggle BVH acceleration structure.
     * @param showMotion  Toggle motion-vector debug mode.
     * @param bvhPicker   UI state for BVH model selection.
     * @param envPicker   UI state for environment map selection.
     */
    void Draw(RenderParams &params, const rt::FrameState &frame, const io::InputState &input, bool &rayMode,
              bool &useBVH, bool &showMotion, BvhModelPickerState &bvhPicker, EnvMapPickerState &envPicker);

    /**
     * @brief Appends a message to the UI log window.
     *
     * Supports printf-style formatting.
     *
     * @param fmt Format string.
     * @param ... Format arguments.
     */
    void Log(const char *fmt, ...);
} // namespace ui
