#include "app/application.h"

#include "app/paths.h"
#include "io/input.h"
#include "render/cubemap.h"
#include "render/render.h"
#include "scene/bvh.h"
#include "ui/gui.h"

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

// ============================================================================
// Application namespace – local helpers
// ============================================================================
// Small utility functions that are only used inside this translation unit.
/// Internal helpers for Application.cpp (private logic, not part of public API).
namespace app_detail {
    // Halton sequence (1D) for a given index and base.
    // Used as a low-discrepancy source for jitter.
    float halton(int index, int base) {
        float f = 1.0f;
        float r = 0.0f;
        while (index > 0) {
            f *= 0.5f;
            const int digit = index % base;
            r += f * static_cast<float>(digit);
            index /= base;
        }
        return r;
    }

    // Generate a 2D jitter sample in [-0.5, 0.5]^2 from the frame index.
    // The mask keeps the sequence bounded to 1024 entries.
    glm::vec2 generateJitter2D(int frameIndex) {
        const int idx = frameIndex & 1023;
        const float jx = halton(idx + 1, 2) - 0.5f;
        const float jy = halton(idx + 1, 3) - 0.5f;
        return {jx, jy};
    }

    // Checks whether any RenderParams value changed between previous and current GUI frame.
    // If anything relevant differs, we reset accumulation.
    bool paramsChanged(const RenderParams &a, const RenderParams &b) {
        auto diff = [](float x, float y) { return std::fabs(x - y) > 1e-5f; };

        // --- Core / toggles ---
        if (a.sppPerFrame != b.sppPerFrame) return true;
        if (a.enableGI != b.enableGI) return true;
        if (a.enableAO != b.enableAO) return true;
        if (a.enableTAA != b.enableTAA) return true;
        if (a.enableSVGF != b.enableSVGF) return true;
        if (a.aoSamples != b.aoSamples) return true;
        if (a.enableEnvMap != b.enableEnvMap) return true;
        if (a.enableJitter != b.enableJitter) return true;

        // --- Albedo material ---
        if (diff(a.matAlbedoColor[0], b.matAlbedoColor[0])) return true;
        if (diff(a.matAlbedoColor[1], b.matAlbedoColor[1])) return true;
        if (diff(a.matAlbedoColor[2], b.matAlbedoColor[2])) return true;
        if (diff(a.matAlbedoSpecStrength, b.matAlbedoSpecStrength)) return true;
        if (diff(a.matAlbedoGloss, b.matAlbedoGloss)) return true;

        // --- Glass ---
        if (a.matGlassEnabled != b.matGlassEnabled) return true;
        if (diff(a.matGlassColor[0], b.matGlassColor[0])) return true;
        if (diff(a.matGlassColor[1], b.matGlassColor[1])) return true;
        if (diff(a.matGlassColor[2], b.matGlassColor[2])) return true;
        if (diff(a.matGlassIOR, b.matGlassIOR)) return true;
        if (diff(a.matGlassDistortion, b.matGlassDistortion)) return true;

        // --- Mirror ---
        if (a.matMirrorEnabled != b.matMirrorEnabled) return true;
        if (diff(a.matMirrorColor[0], b.matMirrorColor[0])) return true;
        if (diff(a.matMirrorColor[1], b.matMirrorColor[1])) return true;
        if (diff(a.matMirrorColor[2], b.matMirrorColor[2])) return true;
        if (diff(a.matMirrorGloss, b.matMirrorGloss)) return true;

        // --- Env / jitter / GI / AO / TAA / SVGF ---
        if (diff(a.envMapIntensity, b.envMapIntensity)) return true;
        if (diff(a.jitterStillScale, b.jitterStillScale)) return true;
        if (diff(a.jitterMovingScale, b.jitterMovingScale)) return true;
        if (diff(a.giScaleAnalytic, b.giScaleAnalytic)) return true;
        if (diff(a.giScaleBVH, b.giScaleBVH)) return true;
        if (diff(a.aoRadius, b.aoRadius)) return true;
        if (diff(a.aoBias, b.aoBias)) return true;
        if (diff(a.aoMin, b.aoMin)) return true;
        if (diff(a.taaStillThresh, b.taaStillThresh)) return true;
        if (diff(a.taaHardMovingThresh, b.taaHardMovingThresh)) return true;
        if (diff(a.taaHistoryMinWeight, b.taaHistoryMinWeight)) return true;
        if (diff(a.taaHistoryAvgWeight, b.taaHistoryAvgWeight)) return true;
        if (diff(a.taaHistoryMaxWeight, b.taaHistoryMaxWeight)) return true;
        if (diff(a.taaHistoryBoxSize, b.taaHistoryBoxSize)) return true;
        if (diff(a.svgfStrength, b.svgfStrength)) return true;
        if (diff(a.svgfVarMax, b.svgfVarMax)) return true;
        if (diff(a.svgfKVar, b.svgfKVar)) return true;
        if (diff(a.svgfKColor, b.svgfKColor)) return true;
        if (diff(a.svgfKVarMotion, b.svgfKVarMotion)) return true;
        if (diff(a.svgfKColorMotion, b.svgfKColorMotion)) return true;
        if (diff(a.svgfVarEPS, b.svgfVarEPS)) return true;
        if (diff(a.svgfMotionEPS, b.svgfMotionEPS)) return true;

        // --- Sun light ---
        if (a.sunEnabled != b.sunEnabled) return true;
        if (diff(a.sunColor[0], b.sunColor[0])) return true;
        if (diff(a.sunColor[1], b.sunColor[1])) return true;
        if (diff(a.sunColor[2], b.sunColor[2])) return true;
        if (diff(a.sunIntensity, b.sunIntensity)) return true;
        if (diff(a.sunYaw, b.sunYaw)) return true;
        if (diff(a.sunPitch, b.sunPitch)) return true;

        // --- Sky dome ---
        if (a.skyEnabled != b.skyEnabled) return true;
        if (diff(a.skyColor[0], b.skyColor[0])) return true;
        if (diff(a.skyColor[1], b.skyColor[1])) return true;
        if (diff(a.skyColor[2], b.skyColor[2])) return true;
        if (diff(a.skyIntensity, b.skyIntensity)) return true;
        if (diff(a.skyYaw, b.skyYaw)) return true;
        if (diff(a.skyPitch, b.skyPitch)) return true;

        // --- Point light ---
        if (a.pointLightEnabled != b.pointLightEnabled) return true;
        if (diff(a.pointLightColor[0], b.pointLightColor[0])) return true;
        if (diff(a.pointLightColor[1], b.pointLightColor[1])) return true;
        if (diff(a.pointLightColor[2], b.pointLightColor[2])) return true;
        if (diff(a.pointLightIntensity, b.pointLightIntensity)) return true;

        if (diff(a.pointLightPos[0], b.pointLightPos[0])) return true;
        if (diff(a.pointLightPos[1], b.pointLightPos[1])) return true;
        if (diff(a.pointLightPos[2], b.pointLightPos[2])) return true;

        if (a.pointLightOrbitEnabled != b.pointLightOrbitEnabled) return true;
        if (diff(a.pointLightOrbitRadius, b.pointLightOrbitRadius)) return true;
        if (diff(a.pointLightOrbitSpeed, b.pointLightOrbitSpeed)) return true;

        if (diff(a.pointLightYaw, b.pointLightYaw)) return true;
        if (diff(a.pointLightPitch, b.pointLightPitch)) return true;

        return false;
    }
} // namespace app_detail

// ============================================================================
// Application lifecycle
// ============================================================================

Application::Application() = default;

Application::~Application() {
    // Make sure we always clean up GL + GLFW on destruction.
    shutdown();
}

// ----------------------------------------------------------------------------
// Window + GL context init
// ----------------------------------------------------------------------------
bool Application::initWindow() {
    if (!glfwInit()) return false;

    // Request a core 4.1 context (compatible with macOS).
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // Fixed-size window for now (1920x1080).
    window = glfwCreateWindow(1920, 1080, "OpenGL Ray/Path Tracing - Darky", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    // Load GL entry points using glad.
    if (!gladLoadGL(glfwGetProcAddress)) {
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// GL resources (FBOs, VAO) that depend on the framebuffer size
// ----------------------------------------------------------------------------
void Application::initGLResources() {
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);

    // Accumulation + GBuffer need to match the actual framebuffer size.
    app.accum.recreate(fbw, fbh);
    app.gBuffer.recreate(fbw, fbh);

    // Fullscreen triangle VAO (no VBO needed).
    glGenVertexArrays(1, &app.fsVao);
}

// ----------------------------------------------------------------------------
// High-level app state: callbacks, shaders, models, env map, input, frame state
// ----------------------------------------------------------------------------
void Application::initState() {
    // Input & callbacks -------------------------------------------------------
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    // Attach AppState to the window, so callbacks can access it.
    glfwSetWindowUserPointer(window, &app);

    // Resize handler: keeps camera aspect, accumulation + GBuffer in sync.
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow *win, int width, int height) {
        if (width <= 0 || height <= 0) return;

        glViewport(0, 0, width, height);
        glScissor(0, 0, width, height);

        if (auto *payload = static_cast<AppState *>(glfwGetWindowUserPointer(win))) {
            payload->camera.AspectRatio = static_cast<float>(width) / static_cast<float>(height);
            payload->accum.recreate(width, height);
            payload->gBuffer.recreate(width, height);
        }
    });

    // Register keyboard / mouse callbacks.
    io::attach_callbacks(window);

    // UI init -----------------------------------------------------------------
    const GLubyte *glVer = glGetString(GL_VERSION);
    ui::Log("[INIT] OpenGL version: %s\n",
            glVer ? reinterpret_cast<const char *>(glVer) : "unknown");
    ui::Init(window);

    // Shaders -----------------------------------------------------------------
    // Resolve paths depending on whether we are running from the build or source tree.
    const std::string rtVertPath = util::resolve_path("shaders/rt/rt_fullscreen.vert");
    const std::string rtFragPath = util::resolve_path("shaders/rt/rt.frag");
    const std::string presentFragPath = util::resolve_path("shaders/rt/rt_present.frag");
    const std::string rasterVertPath = util::resolve_path("shaders/basic.vert");
    const std::string rasterFragPath = util::resolve_path("shaders/basic.frag");

    app.rtShader = std::make_unique<Shader>(rtVertPath.c_str(), rtFragPath.c_str());
    app.presentShader = std::make_unique<Shader>(rtVertPath.c_str(), presentFragPath.c_str());
    app.rasterShader = std::make_unique<Shader>(rasterVertPath.c_str(), rasterFragPath.c_str());

    // If any shader failed, abort early and close the window.
    if (!app.rtShader->isValid() || !app.presentShader->isValid() || !app.rasterShader->isValid()) {
        ui::Log("[INIT] Shader compile/link failed. Exiting.\n");
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    // Models / BVH ------------------------------------------------------------
    // Basic analytic scene geometry + BVH model for the triangle path.
    app.ground = std::make_unique<Model>(util::resolve_path("models/plane.obj"));
    app.bunny = std::make_unique<Model>(util::resolve_path("models/bunny_lp.obj"));
    app.sphere = std::make_unique<Model>(util::resolve_path("models/sphere.obj"));
    app.bvhModel = std::make_unique<Model>(util::resolve_path("models/bunny_lp.obj"));

    const std::string initModelPath = util::resolve_path("models/bunny_lp.obj");
    std::snprintf(app.bvhPicker.currentPath,
                  sizeof(app.bvhPicker.currentPath),
                  "%s",
                  initModelPath.c_str());

    // Build an initial BVH from the default bunny model.
    rebuild_bvh_from_model_path(app.bvhPicker.currentPath,
                                app.bvhTransform,
                                app.bvhModel,
                                app.bvhNodeCount,
                                app.bvhTriCount,
                                app.bvh);

    // Environment map ---------------------------------------------------------
    // Start with a dummy cubemap so shaders always have a valid texture bound.
    app.envMapTex = createDummyCubeMap(); // non-zero texture, GL-driver friendly

    const std::string envDir = util::resolve_dir("cubemaps");
    const std::string defaultEnvPath = envDir + "/Sky_16.png";

    std::snprintf(app.envPicker.currentPath,
                  sizeof(app.envPicker.currentPath),
                  "%s",
                  defaultEnvPath.c_str());
    app.envPicker.selectedIndex = 0;
    app.envPicker.reloadRequested = false;

    // Try to replace the dummy cubemap with a real one.
    const GLuint realEnv = loadCubeMapFromCross(defaultEnvPath);
    if (realEnv != 0) {
        glDeleteTextures(1, &app.envMapTex);
        app.envMapTex = realEnv;
        app.params.enableEnvMap = 1;
        ui::Log("[ENV] Loaded startup cubemap: %s\n", defaultEnvPath.c_str());
    } else {
        app.params.enableEnvMap = 0;
        ui::Log("[ENV] Failed to load startup cubemap '%s', using dummy 1x1 cube.\n",
                defaultEnvPath.c_str());
    }

    // Input mirroring ---------------------------------------------------------
    // Sync GUI-controlled parameters into the input state, so hotkeys can modify them.
    app.input.sppPerFrame = app.params.sppPerFrame;
    app.input.exposure = app.params.exposure;
    app.input.sceneInputEnabled = true;
    app.input.firstMouse = true;
    io::init(app.input);

    // Frame state -------------------------------------------------------------
    // Initialize the frame state so TAA / motion have a valid "previous" frame.
    const glm::mat4 initView = app.camera.GetViewMatrix();
    const glm::mat4 initProj = app.camera.GetProjectionMatrix();
    app.frame.beginFrame(initView, initProj, app.camera.Position);
    app.frame.endFrame();

    app.lastFrame = static_cast<float>(glfwGetTime());
}

// ============================================================================
// Main loop
// ============================================================================
void Application::mainLoop() {
    int fbw = 0, fbh = 0;

    while (!glfwWindowShouldClose(window)) {
        // --------------------------------------------------------------------
        // 1. Time + begin UI frame
        // --------------------------------------------------------------------
        glfwPollEvents();
        ui::BeginFrame();

        const auto tNow = static_cast<float>(glfwGetTime());
        app.deltaTime = tNow - app.lastFrame;
        app.lastFrame = tNow;

        // Point-light orbit animation (deg/s * s).
        // This only updates the light yaw; the actual position is derived in the shader.
        if (app.params.pointLightOrbitEnabled) {
            app.params.pointLightYaw += app.params.pointLightOrbitSpeed * app.deltaTime;

            if (app.params.pointLightYaw > 360.0f) app.params.pointLightYaw -= 360.0f;
            if (app.params.pointLightYaw < -360.0f) app.params.pointLightYaw += 360.0f;
        }

        // --------------------------------------------------------------------
        // 2. Input / camera update
        // --------------------------------------------------------------------
        const bool anyChanged = io::update(app.input, window);
        const bool cameraChangedFromZoom = app.input.cameraChangedThisFrame;

        // Pointer lock toggle (P) – switch between UI interaction and scene control.
        if (app.input.toggledPointerMode) {
            app.input.sceneInputEnabled = !app.input.sceneInputEnabled;
            ui::Log("[INPUT] Scene input %s (mouse %s)\n",
                    app.input.sceneInputEnabled ? "ENABLED" : "DISABLED",
                    app.input.sceneInputEnabled ? "captured" : "released");

            glfwSetInputMode(window,
                             GLFW_CURSOR,
                             app.input.sceneInputEnabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            if (app.input.sceneInputEnabled)
                app.input.firstMouse = true;
        }

        // ESC close request.
        if (app.input.quitRequested)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        // Camera movement only when scene input is enabled.
        if (app.input.sceneInputEnabled)
            app.camera.ProcessKeyboardInput(window, app.deltaTime);

        // --------------------------------------------------------------------
        // 3. Build frame state (view/proj, motion, jitter)
        // --------------------------------------------------------------------
        const glm::mat4 currView = app.camera.GetViewMatrix();
        const glm::mat4 currProj = app.camera.GetProjectionMatrix();
        app.frame.beginFrame(currView, currProj, app.camera.Position);

        // Check how much the view-projection matrix changed since last frame.
        // This drives the "cameraMoved" flag used by TAA and jitter scaling.
        float vpDiff = 0.0f;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                vpDiff = std::max(vpDiff,
                                  std::fabs(app.frame.currViewProj[c][r] -
                                            app.frame.prevViewProj[c][r]));
            }
        }
        const bool cameraMoved = (vpDiff > 1e-5f);

        // Jitter based on camera motion: smaller when still, larger when moving.
        if (app.params.enableJitter) {
            glm::vec2 baseJitter = app_detail::generateJitter2D(app.accum.frameIndex);
            const float scale =
                    cameraMoved ? app.params.jitterMovingScale : app.params.jitterStillScale;
            app.frame.jitter = baseJitter * scale;
        } else {
            app.frame.jitter = glm::vec2(0.0f);
        }

        // --------------------------------------------------------------------
        // 4. Hotkey-driven state changes (modes, SPP, exposure, motion debug)
        // --------------------------------------------------------------------
        if (anyChanged) {
            if (app.input.toggledRayMode) {
                app.rayMode = !app.rayMode;
                app.accum.reset();
            }

            if (app.input.resetAccum) {
                app.accum.reset();
            }

            if (app.input.toggledBVH) {
                app.useBVH = !app.useBVH;
                app.accum.reset();
            }

            if (app.input.changedSPP) {
                app.params.sppPerFrame =
                        std::clamp(app.input.sppPerFrame, 1, 16);
                app.accum.reset();
            }

            if (app.params.exposure != app.input.exposure) {
                app.params.exposure =
                        std::clamp(app.input.exposure, 0.01f, 8.0f);
            }

            if (app.input.toggledMotionDebug) {
                app.showMotion = !app.showMotion;
                app.accum.reset();
            }
        }

        // --------------------------------------------------------------------
        // 5. Rendering (ray or raster)
        // --------------------------------------------------------------------
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glScissor(0, 0, fbw, fbh);

        glClearColor(0.1f, 0.0f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Choose between the ray/path tracer and the simple raster path.
        if (app.rayMode) {
            renderRay(app, fbw, fbh, cameraMoved, currView, currProj);
        } else {
            renderRaster(app, fbw, fbh, currView, currProj);
        }

        app.frame.endFrame();

        // --------------------------------------------------------------------
        // 6. GUI (ImGui) – this can change RenderParams, mode toggles, pickers
        // --------------------------------------------------------------------
        RenderParams prevGuiParams = app.params;
        const bool prevRayMode = app.rayMode;
        const bool prevUseBVH = app.useBVH;
        const bool prevShowMotion = app.showMotion;

        ui::Draw(app.params,
                 app.frame,
                 app.input,
                 app.rayMode,
                 app.useBVH,
                 app.showMotion,
                 app.bvhPicker,
                 app.envPicker);
        ui::EndFrame();

        // --------------------------------------------------------------------
        // 7. Async reloads (BVH, environment map)
        // --------------------------------------------------------------------
        if (app.bvhPicker.reloadRequested) {
            app.bvhPicker.reloadRequested = false;

            if (rebuild_bvh_from_model_path(app.bvhPicker.currentPath,
                                            app.bvhTransform,
                                            app.bvhModel,
                                            app.bvhNodeCount,
                                            app.bvhTriCount,
                                            app.bvh)) {
                ui::Log("[BVH] Rebuilt BVH from '%s': nodes=%d, tris=%d\n",
                        app.bvhPicker.currentPath,
                        app.bvhNodeCount,
                        app.bvhTriCount);
                app.accum.reset();
            } else {
                ui::Log("[BVH] Failed to build BVH from '%s'\n",
                        app.bvhPicker.currentPath);
            }
        }

        if (app.envPicker.reloadRequested) {
            app.envPicker.reloadRequested = false;

            const GLuint newTex = loadCubeMapFromCross(app.envPicker.currentPath);
            if (newTex != 0) {
                if (app.envMapTex) {
                    glDeleteTextures(1, &app.envMapTex);
                }
                app.envMapTex = newTex;
                ui::Log("[ENV] Loaded cubemap: %s\n", app.envPicker.currentPath);
                app.accum.reset();
            } else {
                ui::Log("[ENV] FAILED to load cubemap: %s\n", app.envPicker.currentPath);
            }
        }

        // --------------------------------------------------------------------
        // 8. Present + accumulation reset logic
        // --------------------------------------------------------------------
        glfwSwapBuffers(window);

        const bool guiChangedMode =
                (app.rayMode != prevRayMode) ||
                (app.useBVH != prevUseBVH) ||
                (app.showMotion != prevShowMotion);

        const bool guiChangedParams = app_detail::paramsChanged(app.params, prevGuiParams);

        // Log TAA/SVGF toggle changes explicitly for debugging.
        if (app.params.enableTAA != prevGuiParams.enableTAA) {
            ui::Log("[TAA] %s\n", app.params.enableTAA ? "ENABLED" : "DISABLED");
        }
        if (app.params.enableSVGF != prevGuiParams.enableSVGF) {
            ui::Log("[SVGF] %s\n", app.params.enableSVGF ? "ENABLED" : "DISABLED");
        }

        // Treat an orbiting point light as dynamic geometry for accumulation.
        const bool dynamicPointLightMoving =
                app.rayMode &&
                (app.params.pointLightOrbitEnabled != 0) &&
                (std::fabs(app.params.pointLightOrbitSpeed) > 1e-5f) &&
                (app.params.pointLightOrbitRadius > 0.0f);

        // Any of these conditions invalidate the history buffer.
        if (guiChangedMode || guiChangedParams || cameraChangedFromZoom || dynamicPointLightMoving) {
            app.accum.reset();
            ui::Log("[ACCUM] Reset due to %s%s%s%s\n",
                    guiChangedMode ? "mode " : "",
                    guiChangedParams ? "params " : "",
                    cameraChangedFromZoom ? "zoom " : "",
                    dynamicPointLightMoving ? "dynamicPointLight" : "");
        }
    }
}

// ============================================================================
// Shutdown + run
// ============================================================================
void Application::shutdown() {
    // If init() never completed, only destroy the window + GLFW safely.
    if (!initialized) {
        if (window) {
            glfwDestroyWindow(window);
            window = nullptr;
        }
        glfwTerminate();
        return;
    }

    // Destroy CPU-side wrappers before killing GL objects.
    app.rtShader.reset();
    app.presentShader.reset();
    app.rasterShader.reset();
    app.ground.reset();
    app.bunny.reset();
    app.sphere.reset();
    app.bvhModel.reset();

    // Release environment cubemap if we own one.
    if (app.envMapTex) {
        glDeleteTextures(1, &app.envMapTex);
        app.envMapTex = 0;
    }

    // Fullscreen VAO used by the ray/present passes.
    if (app.fsVao) {
        glDeleteVertexArrays(1, &app.fsVao);
        app.fsVao = 0;
    }

    // GPU-side BVH + GBuffer + accumulation textures.
    app.bvh.release();
    app.gBuffer.release();
    app.accum.release();

    // Tear down ImGui/GUI.
    ui::Shutdown();

    // Finally, destroy the window + GLFW.
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
    initialized = false;
}

int Application::run() {
    // High-level entry point: init, run main loop, then rely on destructor for cleanup.
    if (!initWindow()) return -1;
    initGLResources();
    initState();
    initialized = true;
    mainLoop();
    return 0;
}
