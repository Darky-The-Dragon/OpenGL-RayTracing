// include/io/input.h
#pragma once
#include <GLFW/glfw3.h>

class Camera; // fwd

namespace io {
    struct InputState {
        // runtime-tunable params
        int sppPerFrame = 1; // 1/2/4/8/16
        float exposure = 1.0f;

        // edge-trigger state
        bool prevF2 = false, prevR = false, prevF3 = false, prevF5 = false;

        // toggles from this frame
        bool toggledRayMode = false; // F2
        bool resetAccum = false; // R
        bool cycledSPP = false; // F3
        bool toggledBVH = false; // F5
        bool changedSPP = false; // via 1..4/↑/↓
        bool quitRequested = false; // ESC

        // mouse state (moved here from main)
        bool firstMouse = true;
        float lastX = 400.0f, lastY = 300.0f;
    };

    // Initialize once (placeholder for future use)
    inline void init(InputState &) {
    }

    // Returns true if anything changed that should reset accumulation
    bool update(InputState &s, GLFWwindow *win, float dtSeconds);

    // Hook GLFW callbacks for mouse/scroll (uses glfwSetWindowUserPointer under the hood)
    void attach_callbacks(GLFWwindow *window, Camera *cam, InputState *state);
} // namespace io
