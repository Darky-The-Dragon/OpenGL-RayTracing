#include "io/input.h"
#include "camera/Camera.h"
#include <algorithm>

namespace io {
    // ====== keyboard helper ======
    static inline bool keyDown(GLFWwindow *w, int key) {
        return glfwGetKey(w, key) == GLFW_PRESS;
    }

    bool update(InputState &s, GLFWwindow *win, float dt) {
        bool changed = false;

        // reset per-frame toggles
        s.toggledRayMode = false;
        s.resetAccum = false;
        s.cycledSPP = false;
        s.toggledBVH = false;
        s.changedSPP = false;
        s.toggledMotionDebug = false;
        s.toggledPointerMode = false;

        // ESC → request quit
        if (keyDown(win, GLFW_KEY_ESCAPE))
            s.quitRequested = true;

        // F2 toggle ray/raster
        const bool nowF2 = keyDown(win, GLFW_KEY_F2);
        if (nowF2 && !s.prevF2) {
            s.toggledRayMode = true;
            changed = true;
        }
        s.prevF2 = nowF2;

        // R reset accumulation
        const bool nowR = keyDown(win, GLFW_KEY_R);
        if (nowR && !s.prevR) {
            s.resetAccum = true;
            changed = true;
        }
        s.prevR = nowR;

        // F5 toggle BVH
        const bool nowF5 = keyDown(win, GLFW_KEY_F5);
        if (nowF5 && !s.prevF5) {
            s.toggledBVH = true;
            changed = true;
        }
        s.prevF5 = nowF5;

        // F6: motion-debug toggle (TAA / motion visualization)
        const bool nowF6 = keyDown(win, GLFW_KEY_F6);
        if (nowF6 && !s.prevF6) {
            s.toggledMotionDebug = true;
            changed = true;
        }
        s.prevF6 = nowF6;

        // P: toggle pointer / UI mode (sceneInputEnabled)
        const bool nowP = keyDown(win, GLFW_KEY_P);
        if (nowP && !s.prevP) {
            s.toggledPointerMode = true;
            changed = true;
        }
        s.prevP = nowP;

        // F3 cycle SPP 1→2→4→8→16→1
        const bool nowF3 = keyDown(win, GLFW_KEY_F3);
        if (nowF3 && !s.prevF3) {
            s.sppPerFrame = (s.sppPerFrame == 1)
                                ? 2
                                : (s.sppPerFrame == 2)
                                      ? 4
                                      : (s.sppPerFrame == 4)
                                            ? 8
                                            : (s.sppPerFrame == 8)
                                                  ? 16
                                                  : 1;
            s.cycledSPP = s.changedSPP = true;
            changed = true;
        }
        s.prevF3 = nowF3;

        // Direct SPP hotkeys: ↑/↓ and 1..4
        if (keyDown(win, GLFW_KEY_UP)) {
            const int old = s.sppPerFrame;
            s.sppPerFrame = (old < 2)
                                ? 2
                                : (old < 4)
                                      ? 4
                                      : (old < 8)
                                            ? 8
                                            : (old < 16)
                                                  ? 16
                                                  : 16;
            if (s.sppPerFrame != old) {
                s.changedSPP = true;
                changed = true;
            }
        }
        if (keyDown(win, GLFW_KEY_DOWN)) {
            const int old = s.sppPerFrame;
            s.sppPerFrame = (old > 8)
                                ? 8
                                : (old > 4)
                                      ? 4
                                      : (old > 2)
                                            ? 2
                                            : 1;
            if (s.sppPerFrame != old) {
                s.changedSPP = true;
                changed = true;
            }
        }
        if (keyDown(win, GLFW_KEY_1)) {
            s.sppPerFrame = 2;
            s.changedSPP = true;
            changed = true;
        }
        if (keyDown(win, GLFW_KEY_2)) {
            s.sppPerFrame = 4;
            s.changedSPP = true;
            changed = true;
        }
        if (keyDown(win, GLFW_KEY_3)) {
            s.sppPerFrame = 8;
            s.changedSPP = true;
            changed = true;
        }
        if (keyDown(win, GLFW_KEY_4)) {
            s.sppPerFrame = 16;
            s.changedSPP = true;
            changed = true;
        }

        // Exposure: [ / ]
        if (keyDown(win, GLFW_KEY_LEFT_BRACKET)) {
            s.exposure = std::max(0.05f, s.exposure * 0.97f);
            changed = true;
        }
        if (keyDown(win, GLFW_KEY_RIGHT_BRACKET)) {
            s.exposure = std::min(8.0f, s.exposure * 1.03f);
            changed = true;
        }

        (void) dt;
        return changed;
    }

    // ====== mouse & scroll callbacks ======
    struct CallbackPayload {
        Camera *cam = nullptr;
        InputState *state = nullptr;
    };

    static void mouse_cb(GLFWwindow *w, double xpos, double ypos) {
        auto *p = static_cast<CallbackPayload *>(glfwGetWindowUserPointer(w));
        if (!p || !p->cam || !p->state) return;

        auto &s = *p->state;

        // If UI / pointer mode is active, ignore camera look
        if (!s.sceneInputEnabled) {
            // Still track lastX/lastY to avoid a big jump when re-enabling
            s.lastX = static_cast<float>(xpos);
            s.lastY = static_cast<float>(ypos);
            return;
        }

        if (s.firstMouse) {
            s.lastX = static_cast<float>(xpos);
            s.lastY = static_cast<float>(ypos);
            s.firstMouse = false;
        }

        float dx = static_cast<float>(xpos) - s.lastX;
        float dy = s.lastY - static_cast<float>(ypos);

        s.lastX = static_cast<float>(xpos);
        s.lastY = static_cast<float>(ypos);

        p->cam->ProcessMouseMovement(dx, dy);
    }

    static void scroll_cb(GLFWwindow *w, double /*xoff*/, double yoff) {
        auto *p = static_cast<CallbackPayload *>(glfwGetWindowUserPointer(w));
        if (!p || !p->cam) return;

        p->cam->Fov -= static_cast<float>(yoff) * 2.0f;
        if (p->cam->Fov < 20.0f) p->cam->Fov = 20.0f;
        if (p->cam->Fov > 90.0f) p->cam->Fov = 90.0f;
        // No accumulation reset here either.
    }

    static CallbackPayload gPayload; // single-window app

    void attach_callbacks(GLFWwindow *window, Camera *cam, InputState *state) {
        gPayload.cam = cam;
        gPayload.state = state;
        glfwSetWindowUserPointer(window, &gPayload);
        glfwSetCursorPosCallback(window, mouse_cb);
        glfwSetScrollCallback(window, scroll_cb);
    }
} // namespace io
