#pragma once

class AppState; // fwd
class Camera; // fwd
struct GLFWwindow; // fwd

namespace io {
    struct InputState {
        // runtime-tunable params
        int sppPerFrame = 1; // 1/2/4/8/16
        float exposure = 1.0f;

        // edge-trigger state
        bool prevF2 = false;
        bool prevR = false;
        bool prevP = false;
        bool prevF3 = false;
        bool prevF5 = false;
        bool prevF6 = false; // local motion-debug toggle (F6)

        // toggles from this frame
        bool toggledRayMode = false; // F2
        bool resetAccum = false; // R
        bool cycledSPP = false; // F3
        bool toggledBVH = false; // F5
        bool changedSPP = false; // via 1..4/↑/↓
        bool toggledMotionDebug = false; // F6 – handled in main, but edge-detected here
        bool toggledPointerMode = false; // toggle with P
        bool quitRequested = false; // ESC

        // mouse state
        bool firstMouse = true;
        float lastX = 400.0f;
        float lastY = 300.0f;
        bool sceneInputEnabled = true;

        // Set when zoom/FOV changes this frame (mouse wheel)
        bool cameraChangedThisFrame = false;
    };

    // Initialize once (placeholder for future use)
    inline void init(InputState &) {
    }

    // Returns true if anything changed that should reset accumulation
    bool update(InputState &s, GLFWwindow *win);

    // Hook GLFW callbacks for mouse/scroll
    void attach_callbacks(GLFWwindow *window);
} // namespace io
