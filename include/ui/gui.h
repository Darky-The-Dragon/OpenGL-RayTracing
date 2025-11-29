#pragma once

#include <GLFW/glfw3.h>
#include "render/RenderParams.h"
#include "render/frame_state.h"
#include "io/input.h"

namespace ui {
    // Must be called once after you have a valid OpenGL context + GLFW window.
    void Init(GLFWwindow *window);

    // Called once before shutdown.
    void Shutdown();

    // Call at the start of each frame, before you render your scene.
    void BeginFrame();

    // Call at the end of the frame, AFTER you've rendered your scene to the back-buffer.
    void EndFrame();

    struct BvhModelPickerState {
        bool reloadRequested = false;
        int selectedIndex = 0;
        char currentPath[256] = "../models/bunny_lp.obj";
    };

    struct EnvMapPickerState {
        bool reloadRequested = false;
        int selectedIndex = 0;
        char currentPath[256] = "../cubemaps/Sky_16.png";
    };

    // Draw the control panel.
    //  - We MODIFY RenderParams (sliders / checkboxes).
    //  - We MODIFY rayMode / useBVH / showMotion via GUI (bool refs).
    //  - InputState is only read (for debug display).
    //  - We MODIFY bvhPicker when BVH is enabled (choose model, request reload).
    void Draw(RenderParams &params, const rt::FrameState &frame, const io::InputState &input, bool &rayMode,
              bool &useBVH, bool &showMotion, BvhModelPickerState &bvhPicker, EnvMapPickerState &envPicker);

    void Log(const char *fmt, ...);
} // namespace ui
