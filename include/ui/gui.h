#pragma once

#include <GLFW/glfw3.h>
#include "rt/RenderParams.h"
#include "rt/frame_state.h"
#include "io/input.h"

namespace ui {
    // Must be called once after you have a valid OpenGL context + GLFW window.
    void Init(GLFWwindow *window);

    // Called once before shutdown.
    void Shutdown();

    // Call at the start of each frame, before you render your scene.
    void BeginFrame();

    // Draw the control panel.
    //
    // Now:
    //  - We MODIFY RenderParams (sliders / checkboxes).
    //  - We MODIFY rayMode / useBVH / showMotion via GUI (bool refs).
    //  - InputState is only read (for debug display).
    void Draw(RenderParams &params, const rt::FrameState &frame, const io::InputState &input, bool &rayMode,
              bool &useBVH, bool &showMotion);

    void Log(const char *fmt, ...);

    // Call at the end of the frame, AFTER you've rendered your scene to the back-buffer.
    void EndFrame();
} // namespace ui
