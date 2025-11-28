#include "app/application.h"
#include "render/cubemap.h"
#include "render/render.h"
#include "scene/bvh.h"
#include "ui/gui.h"
#include "io/input.h"
#include "app/paths.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <string>

static float halton(int index, const int base) {
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

static glm::vec2 generateJitter2D(const int frameIndex) {
    const int idx = frameIndex & 1023;
    float jx = halton(idx + 1, 2) - 0.5f;
    float jy = halton(idx + 1, 3) - 0.5f;
    return {jx, jy};
}

static bool paramsChanged(const RenderParams &a, const RenderParams &b) {
    auto diff = [](float x, float y) { return std::fabs(x - y) > 1e-5f; };

    if (a.sppPerFrame != b.sppPerFrame) return true;
    if (a.enableGI != b.enableGI) return true;
    if (a.enableAO != b.enableAO) return true;
    if (a.enableTAA != b.enableTAA) return true;
    if (a.enableSVGF != b.enableSVGF) return true;
    if (a.aoSamples != b.aoSamples) return true;
    if (a.enableEnvMap != b.enableEnvMap) return true;
    if (a.enableJitter != b.enableJitter) return true;

    // --- Albedo ---
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

    return false;
}

Application::Application() = default;

Application::~Application() {
    shutdown();
}

bool Application::initWindow() {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    window = glfwCreateWindow(1920, 1080, "OpenGL Ray/Path Tracing - Darky", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGL(glfwGetProcAddress)) {
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
        return false;
    }

    return true;
}

void Application::initGLResources() {
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    app.accum.recreate(fbw, fbh);
    app.gBuffer.recreate(fbw, fbh);
    glGenVertexArrays(1, &app.fsVao);
}

void Application::initState() {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetWindowUserPointer(window, &app);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow *win, int width, int height) {
        if (width <= 0 || height <= 0) return;
        glViewport(0, 0, width, height);
        glScissor(0, 0, width, height);
        auto *payload = static_cast<AppState *>(glfwGetWindowUserPointer(win));
        if (payload) {
            payload->camera.AspectRatio = static_cast<float>(width) / static_cast<float>(height);
            payload->accum.recreate(width, height);
            payload->gBuffer.recreate(width, height);
        }
    });
    io::attach_callbacks(window);

    const GLubyte *glVer = glGetString(GL_VERSION);
    ui::Log("[INIT] OpenGL version: %s\n", glVer ? reinterpret_cast<const char *>(glVer) : "unknown");
    ui::Init(window);

    const std::string rtVertPath = util::resolve_path("shaders/rt/rt_fullscreen.vert");
    const std::string rtFragPath = util::resolve_path("shaders/rt/rt.frag");
    const std::string presentFragPath = util::resolve_path("shaders/rt/rt_present.frag");
    const std::string rasterVertPath = util::resolve_path("shaders/basic.vert");
    const std::string rasterFragPath = util::resolve_path("shaders/basic.frag");

    app.rtShader = std::make_unique<Shader>(rtVertPath.c_str(), rtFragPath.c_str());
    app.presentShader = std::make_unique<Shader>(rtVertPath.c_str(), presentFragPath.c_str());
    app.rasterShader = std::make_unique<Shader>(rasterVertPath.c_str(), rasterFragPath.c_str());
    if (!app.rtShader->isValid() || !app.presentShader->isValid() || !app.rasterShader->isValid()) {
        ui::Log("[INIT] Shader compile/link failed. Exiting.\n");
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    app.ground = std::make_unique<Model>(util::resolve_path("models/plane.obj"));
    app.bunny = std::make_unique<Model>(util::resolve_path("models/bunny_lp.obj"));
    app.sphere = std::make_unique<Model>(util::resolve_path("models/sphere.obj"));
    app.bvhModel = std::make_unique<Model>(util::resolve_path("models/bunny_lp.obj"));
    const std::string initModelPath = util::resolve_path("models/bunny_lp.obj");
    std::snprintf(app.bvhPicker.currentPath, sizeof(app.bvhPicker.currentPath), "%s", initModelPath.c_str());

    rebuild_bvh_from_model_path(app.bvhPicker.currentPath, app.bvhTransform, app.bvhModel, app.bvhNodeCount,
                                app.bvhTriCount, app.bvh);

    // --- Environment Map Initialization ---

    // Always create a dummy env cubemap so samplerCube never sees texture 0.
    // This keeps the Apple/Metal GL driver happy (no GLD warnings).
    app.envMapTex = createDummyCubeMap();

    // Resolve the cubemaps directory and build the default env path.
    const std::string envDir = util::resolve_dir("cubemaps");
    const std::string defaultEnvPath = envDir + "/Sky_16.png";

    // Reflect this path in the GUI picker
    std::snprintf(app.envPicker.currentPath, sizeof(app.envPicker.currentPath), "%s", defaultEnvPath.c_str());
    app.envPicker.selectedIndex = 0;
    app.envPicker.reloadRequested = false;

    // Try to load the default envmap once at startup.
    const GLuint realEnv = loadCubeMapFromCross(defaultEnvPath);
    if (realEnv != 0) {
        glDeleteTextures(1, &app.envMapTex); // replace dummy
        app.envMapTex = realEnv;
        app.params.enableEnvMap = 1;
        ui::Log("[ENV] Loaded startup cubemap: %s\n", defaultEnvPath.c_str());
    } else {
        app.params.enableEnvMap = 0;
        ui::Log("[ENV] Failed to load startup cubemap '%s', using dummy 1x1 cube.\n",
                defaultEnvPath.c_str());
    }

    app.input.sppPerFrame = app.params.sppPerFrame;
    app.input.exposure = app.params.exposure;
    app.input.sceneInputEnabled = true;
    app.input.firstMouse = true;
    io::init(app.input);

    const glm::mat4 initView = app.camera.GetViewMatrix();
    const glm::mat4 initProj = app.camera.GetProjectionMatrix();
    app.frame.beginFrame(initView, initProj, app.camera.Position);
    app.frame.endFrame();

    app.lastFrame = static_cast<float>(glfwGetTime());
}

void Application::mainLoop() {
    int fbw = 0, fbh = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ui::BeginFrame();

        const auto tNow = static_cast<float>(glfwGetTime());
        app.deltaTime = tNow - app.lastFrame;
        app.lastFrame = tNow;

        const bool anyChanged = io::update(app.input, window);
        const bool cameraChangedFromZoom = app.input.cameraChangedThisFrame;

        if (app.input.toggledPointerMode) {
            app.input.sceneInputEnabled = !app.input.sceneInputEnabled;
            ui::Log("[INPUT] Scene input %s (mouse %s)\n",
                    app.input.sceneInputEnabled ? "ENABLED" : "DISABLED",
                    app.input.sceneInputEnabled ? "captured" : "released");
            glfwSetInputMode(window, GLFW_CURSOR,
                             app.input.sceneInputEnabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            if (app.input.sceneInputEnabled) app.input.firstMouse = true;
        }
        if (app.input.quitRequested) glfwSetWindowShouldClose(window, GLFW_TRUE);
        if (app.input.sceneInputEnabled) app.camera.ProcessKeyboardInput(window, app.deltaTime);

        const glm::mat4 currView = app.camera.GetViewMatrix();
        const glm::mat4 currProj = app.camera.GetProjectionMatrix();
        app.frame.beginFrame(currView, currProj, app.camera.Position);

        float vpDiff = 0.0f;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                vpDiff = std::max(vpDiff, std::fabs(app.frame.currViewProj[c][r] - app.frame.prevViewProj[c][r]));
        const bool cameraMoved = (vpDiff > 1e-5f);

        if (app.params.enableJitter) {
            glm::vec2 baseJitter = generateJitter2D(app.accum.frameIndex);
            const float scale = cameraMoved ? app.params.jitterMovingScale : app.params.jitterStillScale;
            app.frame.jitter = baseJitter * scale;
        } else {
            app.frame.jitter = glm::vec2(0.0f);
        }

        if (anyChanged) {
            if (app.input.toggledRayMode) {
                app.rayMode = !app.rayMode;
                app.accum.reset();
            }
            if (app.input.resetAccum) { app.accum.reset(); }
            if (app.input.toggledBVH) {
                app.useBVH = !app.useBVH;
                app.accum.reset();
            }
            if (app.input.changedSPP) {
                // Hotkeys support up to 16; clamp here for consistency.
                app.params.sppPerFrame = std::clamp(app.input.sppPerFrame, 1, 16);
                app.accum.reset();
            }
            if (app.params.exposure != app.input.exposure)
                app.params.exposure = std::clamp(
                    app.input.exposure, 0.01f, 8.0f);
            if (app.input.toggledMotionDebug) {
                app.showMotion = !app.showMotion;
                app.accum.reset();
            }
        }

        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glScissor(0, 0, fbw, fbh);
        glClearColor(0.1f, 0.0f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (app.rayMode) renderRay(app, fbw, fbh, cameraMoved, currView, currProj);
        else renderRaster(app, fbw, fbh, currView, currProj);

        app.frame.endFrame();

        RenderParams prevGuiParams = app.params;
        const bool prevRayMode = app.rayMode;
        const bool prevUseBVH = app.useBVH;
        const bool prevShowMotion = app.showMotion;

        ui::Draw(app.params, app.frame, app.input, app.rayMode, app.useBVH, app.showMotion, app.bvhPicker,
                 app.envPicker);
        ui::EndFrame();

        if (app.bvhPicker.reloadRequested) {
            app.bvhPicker.reloadRequested = false;
            if (rebuild_bvh_from_model_path(app.bvhPicker.currentPath, app.bvhTransform, app.bvhModel,
                                            app.bvhNodeCount, app.bvhTriCount, app.bvh)) {
                ui::Log("[BVH] Rebuilt BVH from '%s': nodes=%d, tris=%d\n",
                        app.bvhPicker.currentPath, app.bvhNodeCount, app.bvhTriCount);
                app.accum.reset();
            } else {
                ui::Log("[BVH] Failed to build BVH from '%s'\n", app.bvhPicker.currentPath);
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

        glfwSwapBuffers(window);

        const bool guiChangedMode = (app.rayMode != prevRayMode) || (app.useBVH != prevUseBVH) ||
                                    (app.showMotion != prevShowMotion);
        const bool guiChangedParams = paramsChanged(app.params, prevGuiParams);

        if (app.params.enableTAA != prevGuiParams.enableTAA) {
            ui::Log("[TAA] %s\n", app.params.enableTAA ? "ENABLED" : "DISABLED");
        }
        if (app.params.enableSVGF != prevGuiParams.enableSVGF) {
            ui::Log("[SVGF] %s\n", app.params.enableSVGF ? "ENABLED" : "DISABLED");
        }

        if (guiChangedMode || guiChangedParams || cameraChangedFromZoom) {
            app.accum.reset();
            ui::Log("[ACCUM] Reset due to %s%s%s\n",
                    guiChangedMode ? "mode " : "",
                    guiChangedParams ? "params " : "",
                    cameraChangedFromZoom ? "zoom" : "");
        }
    }
}

void Application::shutdown() {
    if (!initialized) {
        if (window) {
            glfwDestroyWindow(window);
            window = nullptr;
        }
        glfwTerminate();
        return;
    }

    app.rtShader.reset();
    app.presentShader.reset();
    app.rasterShader.reset();
    app.ground.reset();
    app.bunny.reset();
    app.sphere.reset();
    app.bvhModel.reset();

    if (app.envMapTex) {
        glDeleteTextures(1, &app.envMapTex);
        app.envMapTex = 0;
    }

    if (app.fsVao) {
        glDeleteVertexArrays(1, &app.fsVao);
        app.fsVao = 0;
    }
    app.bvh.release();
    app.gBuffer.release();
    app.accum.release();

    ui::Shutdown();

    if (window) glfwDestroyWindow(window);
    glfwTerminate();
    initialized = false;
}

int Application::run() {
    if (!initWindow()) return -1;
    initGLResources();
    initState();
    initialized = true;
    mainLoop();
    return 0;
}
