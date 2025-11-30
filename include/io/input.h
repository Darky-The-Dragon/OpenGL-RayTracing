#pragma once

#include <GLFW/glfw3.h>

class AppState; // fwd
class Camera; // fwd

/// Input system: keyboard/mouse handling, per-frame user state, and camera interaction.
namespace io {
    /**
     * @struct InputState
     * @brief Stores all per-frame and edge-triggered input information.
     *
     * This struct acts as the central input buffer for the entire engine.
     * It tracks runtime-tunable parameters (SPP, exposure), key-edge
     * transitions (e.g., toggling ray mode or BVH), mouse deltas, and
     * state required for pointer-lock / camera control.
     *
     * The separation between "edge-triggered" (press once) and
     * "runtime-tunable" (continuous) fields helps the main loop keep
     * rendering logic deterministic and avoids mixing GLFW queries
     * inside the renderer.
     */
    struct InputState {
        // ---------------------------------------------------------------------
        // Runtime-tunable parameters
        // ---------------------------------------------------------------------

        /// Samples-per-pixel computed per frame (1 / 2 / 4 / 8 / 16).
        int sppPerFrame = 1;

        /// Exposure multiplier applied during tone mapping.
        float exposure = 1.0f;

        // ---------------------------------------------------------------------
        // Edge-trigger memory to detect key presses vs holds
        // ---------------------------------------------------------------------

        bool prevF2 = false; ///< Previous state of F2 (ray mode toggle).
        bool prevR = false; ///< Previous state of R (accumulation reset).
        bool prevP = false; ///< Previous state of P (pointer/scene toggle).
        bool prevF3 = false; ///< Previous state of F3 (cycle SPP).
        bool prevF5 = false; ///< Previous state of F5 (BVH toggle).
        bool prevF6 = false; ///< Previous state of F6 (motion debug toggle).

        // ---------------------------------------------------------------------
        // Toggles and state flags updated this frame
        // ---------------------------------------------------------------------

        bool toggledRayMode = false; ///< Set when ray/raster mode changes (F2).
        bool resetAccum = false; ///< Set on 'R' to force accumulation clear.
        bool cycledSPP = false; ///< SPP increased/decreased this frame.
        bool toggledBVH = false; ///< BVH usage toggled (F5).
        bool changedSPP = false; ///< SPP changed via number keys or arrows.
        bool toggledMotionDebug = false; ///< Toggles motion-vector debug output (F6).
        bool toggledPointerMode = false; ///< Switches pointer lock on/off (P).
        bool quitRequested = false; ///< ESC requested a window close.

        // ---------------------------------------------------------------------
        // Mouse state
        // ---------------------------------------------------------------------

        bool firstMouse = true; ///< True until first mouse callback initializes lastX/Y.
        float lastX = 400.0f; ///< Last recorded mouse X position.
        float lastY = 300.0f; ///< Last recorded mouse Y position.

        /// Whether scene interaction is allowed (disabled when pointer is released).
        bool sceneInputEnabled = true;

        /// Set when zoom/FOV changes this frame via scroll input.
        bool cameraChangedThisFrame = false;
    };

    /**
     * @brief Optional initialization step for future extension.
     *
     * Currently a placeholder, but kept to maintain symmetry with
     * other modules of the engine.
     */
    inline void init(InputState &) {
    }

    /**
     * @brief Processes GLFW input and updates the InputState for this frame.
     *
     * This function handles:
     * - key edge detection (toggles, mode switches, SPP changes)
     * - ESC quit request
     * - FOV changes from scroll input
     * - pointer lock transitions
     *
     * The main loop uses the returned boolean to determine whether
     * the accumulation buffer must be reset (e.g., camera moved, SPP changed).
     *
     * @param s   Reference to the input state that will be updated.
     * @param win Pointer to the GLFW window used for polling.
     * @return True if any input event requires re-render accumulation reset.
     */
    bool update(InputState &s, GLFWwindow *win);

    /**
     * @brief Attaches GLFW mouse and scroll callbacks used for camera control.
     *
     * These callbacks only register raw input data (mouse deltas and scroll
     * offsets). Interpretation—such as updating camera orientation or
     * changing FOV—is handled later inside io::update().
     *
     * @param window GLFW window to attach event callbacks to.
     */
    void attach_callbacks(GLFWwindow *window);
} // namespace io
