#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "camera/Camera.h"
#include "Shader.h"
#include "utils/model.h"
#include "rt/accum.h"
#include "rt/RenderParams.h"
#include "rt/frame_state.h"
#include "rt/gbuffer.h"
#include "bvh.h"
#include "io/input.h"
#include "ui/gui.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <algorithm>
#include <vector>

// ===============================
// Global state
// ===============================
static rt::Accum gAccum; // accumulation ping-pong + frame counter
static rt::GBuffer gGBuffer;
static rt::FrameState gFrame;
static bool gRayMode = true; // F2: raster <-> ray
static RenderParams gParams; // runtime-tweakable render parameters

// Motion debug (F6) – local toggle only
static bool gShowMotion = false;

// Fullscreen triangle
static GLuint gFsVao = 0;

// Shaders
static Shader *gRtShader = nullptr;
static Shader *gPresentShader = nullptr;

// Camera
static float deltaTime = 0.0f;
static float lastFrame = 0.0f;
static Camera camera(
    glm::vec3(0.0f, 2.0f, 8.0f),
    -90.0f, // yaw
    -10.0f, // pitch
    60.0f, // fov
    1920.0f / 1080.0f // aspect
);

// Raster path (unchanged)
static Shader *gRasterShader = nullptr;
static Model *gGround = nullptr;
static Model *gBunny = nullptr;
static Model *gSphere = nullptr;

// BVH data & toggle
static bool gUseBVH = false; // toggled via F5 from io::update()
static GLuint gBvhNodeTex = 0, gBvhNodeBuf = 0;
static GLuint gBvhTriTex = 0, gBvhTriBuf = 0;
static int gBvhNodeCount = 0, gBvhTriCount = 0;

// Model used for BVH building (ray path only)
static Model *gBvhModel = nullptr;

// ImGui BVH model picker state
static ui::BvhModelPickerState gBvhPicker;

// Centralized input state
static io::InputState gInput;

// ===============================
// Callbacks
// ===============================
static void framebuffer_size_callback(GLFWwindow *, const int width, const int height) {
    if (width <= 0 || height <= 0) return;

    glViewport(0, 0, width, height);
    glScissor(0, 0, width, height);

    camera.AspectRatio = static_cast<float>(width) / static_cast<float>(height);

    gAccum.recreate(width, height);
    gGBuffer.recreate(width, height);
}

// --- Jitter helpers (local to main.cpp) ---
// --- Jitter helpers (local to main.cpp) ---
static float halton(int index, const int base) {
    float f = 1.0f;
    float r = 0.0f;

    while (index > 0) {
        f *= 0.5f; // same as f /= base when base==2,3; simple and fast
        const int digit = index % base;
        r += f * static_cast<float>(digit); // avoid int -> float narrowing
        index /= base;
    }

    return r;
}

// Returns jitter in pixel units, in range [-0.5, 0.5]
static glm::vec2 generateJitter2D(const int frameIndex) {
    // wrap to avoid giant indices; 1024-frame cycle is fine
    const int idx = frameIndex & 1023;
    float jx = halton(idx + 1, 2) - 0.5f;
    float jy = halton(idx + 1, 3) - 0.5f;
    return {jx, jy};
}

// Rebuild BVH from a given model path and upload to TBOs
static void rebuildBVHFromModelPath(const char *path) {
    // Free previous GPU resources
    if (gBvhNodeTex) {
        glDeleteTextures(1, &gBvhNodeTex);
        gBvhNodeTex = 0;
    }
    if (gBvhTriTex) {
        glDeleteTextures(1, &gBvhTriTex);
        gBvhTriTex = 0;
    }
    if (gBvhNodeBuf) {
        glDeleteBuffers(1, &gBvhNodeBuf);
        gBvhNodeBuf = 0;
    }
    if (gBvhTriBuf) {
        glDeleteBuffers(1, &gBvhTriBuf);
        gBvhTriBuf = 0;
    }

    // Reload BVH model
    delete gBvhModel;
    gBvhModel = new Model(path);
    if (gBvhModel->meshes.empty()) {
        ui::Log("[BVH] Failed to load model for BVH: %s\n", path);
        gBvhNodeCount = gBvhTriCount = 0;
        return;
    }

    std::vector<CPU_Triangle> triCPU;
    glm::mat4 M(1.0f);
    M = glm::translate(M, glm::vec3(0.0f, 1.0f, -3.0f));
    M = glm::scale(M, glm::vec3(1.0f));
    gather_model_triangles(*gBvhModel, M, triCPU);

    std::vector<BVHNode> nodesCPU = build_bvh(triCPU);
    gBvhNodeCount = static_cast<int>(nodesCPU.size());
    gBvhTriCount = static_cast<int>(triCPU.size());

    upload_bvh_tbo(nodesCPU, triCPU,
                   gBvhNodeTex, gBvhNodeBuf,
                   gBvhTriTex, gBvhTriBuf);

    ui::Log("[BVH] Built BVH from '%s': nodes=%d, tris=%d\n",
            path, gBvhNodeCount, gBvhTriCount);
}

// ===============================
int main() {
    // --- Init GLFW/GL context ---
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow *window = glfwCreateWindow(
        1920, 1080,
        "OpenGL Ray/Path Tracing - Darky",
        nullptr, nullptr
    );
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    gladLoadGL(glfwGetProcAddress);

    glEnable(GL_SCISSOR_TEST); {
        int initW = 0, initH = 0;
        glfwGetFramebufferSize(window, &initW, &initH);
        if (initW > 0 && initH > 0) {
            glViewport(0, 0, initW, initH);
            glScissor(0, 0, initW, initH);
        }
    }

    // Initialize timing so the first frame doesn't get a huge deltaTime
    lastFrame = static_cast<float>(glfwGetTime());

    // Start in FPS-style mode: cursor locked+hidden, scene input enabled
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // Route mouse + scroll to io module (camera only, accumulation controlled in main)
    io::attach_callbacks(window, &camera, &gInput);

    glEnable(GL_DEPTH_TEST);

    // It's safe to log before ImGui init because the console only uses ImGuiTextBuffer.
    const GLubyte *glVer = glGetString(GL_VERSION);
    ui::Log("[INIT] OpenGL version: %s\n",
            glVer ? reinterpret_cast<const char *>(glVer) : "unknown");

    // --- ImGui init ---
    ui::Init(window);

    // Shaders
    gRtShader = new Shader("../shaders/rt/rt_fullscreen.vert", "../shaders/rt/rt.frag");
    gPresentShader = new Shader("../shaders/rt/rt_fullscreen.vert", "../shaders/rt/rt_present.frag");

    // Accumulation buffers + GBuffer
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    gAccum.recreate(fbw, fbh);
    gGBuffer.recreate(fbw, fbh);

    // Fullscreen triangle VAO (no VBO)
    glGenVertexArrays(1, &gFsVao);

    // Raster scene
    gRasterShader = new Shader("../shaders/basic.vert", "../shaders/basic.frag");
    gGround = new Model("../models/plane.obj");
    gBunny = new Model("../models/bunny_lp.obj");
    gSphere = new Model("../models/sphere.obj");

    // BVH model starts as the low-poly bunny
    gBvhModel = new Model("../models/bunny_lp.obj");
    std::snprintf(gBvhPicker.currentPath, sizeof(gBvhPicker.currentPath), "../models/bunny_lp.obj");

    if (gGround->meshes.empty()) ui::Log("[WARN] Failed to load plane.obj\n");
    if (gBunny->meshes.empty()) ui::Log("[WARN] Failed to load bunny_lp.obj (raster)\n");
    if (gSphere->meshes.empty()) ui::Log("[WARN] Failed to load sphere.obj\n");
    if (gBvhModel->meshes.empty()) ui::Log("[WARN] Failed to load bunny_lp.obj (BVH)\n");

    // ---- Build BVH from the initial model, upload as TBOs ----
    rebuildBVHFromModelPath(gBvhPicker.currentPath);

    ui::Log("[BVH] Built BVH: nodes=%d, tris=%d\n", gBvhNodeCount, gBvhTriCount);

    // --- Input + params init ---
    gInput.sppPerFrame = gParams.sppPerFrame;
    gInput.exposure = gParams.exposure;
    gInput.sceneInputEnabled = true; // start with scene input enabled
    gInput.firstMouse = true;
    io::init(gInput);

    ui::Log("[INPUT] Scene input ENABLED (mouse captured)\n");

    // Initialize frame state so prev == curr on first frame
    {
        const glm::mat4 initView = camera.GetViewMatrix();
        const glm::mat4 initProj = camera.GetProjectionMatrix();
        gFrame.beginFrame(initView, initProj, camera.Position);
        gFrame.endFrame(); // prevViewProj = currViewProj, prevCamPos = currCamPos
    }

    // ===============================
    // Main loop
    // ===============================
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start ImGui frame
        ui::BeginFrame();

        // --- Time update ---
        auto tNow = static_cast<float>(glfwGetTime());
        deltaTime = tNow - lastFrame;
        lastFrame = tNow;

        // --- Centralized input update (SPP, exposure, BVH toggle, etc.) ---
        const bool anyChanged = io::update(gInput, window, deltaTime);

        // P: toggle pointer / UI mode (sceneInputEnabled)
        if (gInput.toggledPointerMode) {
            gInput.sceneInputEnabled = !gInput.sceneInputEnabled;

            ui::Log("[INPUT] Scene input %s (mouse %s)\n",
                    gInput.sceneInputEnabled ? "ENABLED" : "DISABLED",
                    gInput.sceneInputEnabled ? "captured" : "released");

            if (gInput.sceneInputEnabled) {
                // Back to FPS mode
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                gInput.firstMouse = true; // avoid camera jump on recapture
            } else {
                // UI / pointer mode
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }

        // ESC → quit
        if (gInput.quitRequested) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // --- Camera movement (keyboard) ---
        if (gInput.sceneInputEnabled) {
            camera.ProcessKeyboardInput(window, deltaTime);
        }

        // --- Matrices for motion vectors (AFTER camera changes this frame) ---
        const glm::mat4 currView = camera.GetViewMatrix();
        const glm::mat4 currProj = camera.GetProjectionMatrix();

        // Update per-frame state (currViewProj, currCamPos, etc.)
        gFrame.beginFrame(currView, currProj, camera.Position);

        // Detect ANY camera change (translation OR rotation OR FOV) via VP matrix diff
        float vpDiff = 0.0f;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                vpDiff = std::max(
                    vpDiff,
                    std::fabs(gFrame.currViewProj[c][r] - gFrame.prevViewProj[c][r])
                );
            }
        }
        const bool cameraMoved = (vpDiff > 1e-5f); // tweak threshold if needed

        // --- Jitter: write into FrameState (pixel units in [-0.5, 0.5]) ---
        if (gParams.enableJitter) {
            glm::vec2 baseJitter = generateJitter2D(gAccum.frameIndex);

            // Smaller jitter when camera is still, larger when moving
            float scale = cameraMoved
                              ? gParams.jitterMovingScale
                              : gParams.jitterStillScale;

            gFrame.jitter = baseJitter * scale;
        } else {
            gFrame.jitter = glm::vec2(0.0f);
        }

        // --- Apply input events that affect accumulation / params ---
        if (anyChanged) {
            if (gInput.toggledRayMode) {
                gRayMode = !gRayMode;
                gAccum.reset();
                ui::Log("[MODE] Ray mode = %s\n",
                        gRayMode ? "RAY TRACING" : "RASTER");
            }

            if (gInput.resetAccum) {
                gAccum.reset();
                ui::Log("[ACCUM] Manual reset (R key)\n");
            }

            if (gInput.toggledBVH) {
                gUseBVH = !gUseBVH;
                gAccum.reset();
                ui::Log("[SCENE] BVH mode %s (nodes=%d, tris=%d)\n",
                        gUseBVH ? "ON" : "OFF",
                        gBvhNodeCount, gBvhTriCount);
            }

            if (gInput.changedSPP) {
                gParams.sppPerFrame = std::max(1, std::min(gInput.sppPerFrame, 64));
                gAccum.reset();
                ui::Log("[INPUT] SPP per frame = %d\n", gParams.sppPerFrame);
            }

            if (gParams.exposure != gInput.exposure) {
                gParams.exposure = std::max(0.01f, std::min(gInput.exposure, 8.0f));
                ui::Log("[INPUT] Exposure = %.3f\n", gParams.exposure);
            }

            if (gInput.toggledMotionDebug) {
                gShowMotion = !gShowMotion;
                gAccum.reset(); // history invalid when switching debug view
                ui::Log("[DEBUG] Motion view = %s\n",
                        gShowMotion ? "ON" : "OFF");
            }
        }

        // --- Snapshot state before GUI modifications (for change detection) ---
        RenderParams prevGuiParams = gParams;
        bool prevRayMode = gRayMode;
        bool prevUseBVH = gUseBVH;
        bool prevShowMotion = gShowMotion;

        // --- Framebuffer size / viewport / clear ---
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glScissor(0, 0, fbw, fbh);

        glClearColor(0.1f, 0.0f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (gRayMode) {
            // ===============================
            // Ray mode: render into accum + motion + GBuffer (WS pos/nrm)
            // ===============================
            gAccum.bindWriteFBO_MRT(gGBuffer.posTex, gGBuffer.nrmTex);
            glViewport(0, 0, fbw, fbh);
            glDepthMask(GL_FALSE);
            glDisable(GL_DEPTH_TEST);

            gRtShader->use();

            // Camera basis
            glm::vec3 right = glm::normalize(glm::vec3(
                currView[0][0], currView[1][0], currView[2][0]));
            glm::vec3 up = glm::normalize(glm::vec3(
                currView[0][1], currView[1][1], currView[2][1]));
            glm::vec3 fwd = -glm::normalize(glm::vec3(
                currView[0][2], currView[1][2], currView[2][2]));
            float tanHalfFov = std::tanf(glm::radians(camera.Fov) * 0.5f);

            // Core camera + accumulation params
            gRtShader->setVec3("uCamPos", camera.Position);
            gRtShader->setVec3("uCamRight", right);
            gRtShader->setVec3("uCamUp", up);
            gRtShader->setVec3("uCamFwd", fwd);
            gRtShader->setFloat("uTanHalfFov", tanHalfFov);
            gRtShader->setFloat("uAspect", camera.AspectRatio);
            gRtShader->setInt("uFrameIndex", gAccum.frameIndex);
            gRtShader->setVec2("uResolution", glm::vec2(fbw, fbh));
            gRtShader->setInt("uSpp", gShowMotion ? 1 : gParams.sppPerFrame);
            gRtShader->setVec2("uJitter", gFrame.jitter);
            gRtShader->setInt("uEnableJitter", gParams.enableJitter ? 1 : 0);

            // Scene / BVH
            gRtShader->setInt("uUseBVH", gUseBVH ? 1 : 0);
            gRtShader->setInt("uNodeCount", gBvhNodeCount);
            gRtShader->setInt("uTriCount", gBvhTriCount);

            // TAA params from RenderParams
            gRtShader->setFloat("uTaaStillThresh", gParams.taaStillThresh);
            gRtShader->setFloat("uTaaHardMovingThresh", gParams.taaHardMovingThresh);
            gRtShader->setFloat("uTaaHistoryMinWeight", gParams.taaHistoryMinWeight);
            gRtShader->setFloat("uTaaHistoryAvgWeight", gParams.taaHistoryAvgWeight);
            gRtShader->setFloat("uTaaHistoryMaxWeight", gParams.taaHistoryMaxWeight);
            gRtShader->setFloat("uTaaHistoryBoxSize", gParams.taaHistoryBoxSize);
            gRtShader->setInt("uEnableTAA", gParams.enableTAA);

            // GI / AO / mirror toggles + scales (from RenderParams)
            gRtShader->setFloat("uGiScaleAnalytic", gParams.giScaleAnalytic);
            gRtShader->setFloat("uGiScaleBVH", gParams.giScaleBVH);
            gRtShader->setInt("uEnableGI", gParams.enableGI);
            gRtShader->setInt("uEnableAO", gParams.enableAO);
            gRtShader->setInt("uEnableMirror", gParams.enableMirror);
            gRtShader->setFloat("uMirrorStrength", gParams.mirrorStrength);
            gRtShader->setInt("uAO_SAMPLES", gParams.aoSamples);
            gRtShader->setFloat("uAO_RADIUS", gParams.aoRadius);
            gRtShader->setFloat("uAO_BIAS", gParams.aoBias);
            gRtShader->setFloat("uAO_MIN", gParams.aoMin);

            // Motion / TAA
            gRtShader->setInt("uShowMotion", gShowMotion ? 1 : 0);
            gRtShader->setInt("uCameraMoved", cameraMoved ? 1 : 0);
            gRtShader->setMat4("uPrevViewProj", gFrame.prevViewProj);
            gRtShader->setMat4("uCurrViewProj", gFrame.currViewProj);
            gRtShader->setVec2("uResolution", glm::vec2(fbw, fbh));

            // Constants
            gRtShader->setFloat("uEPS", gParams.EPS);
            gRtShader->setFloat("uPI", gParams.PI);
            gRtShader->setFloat("uINF", gParams.INF);

            // Accum input (history)
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gAccum.readTex());
            gRtShader->setInt("uPrevAccum", 0);

            // BVH TBOs
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_BUFFER, gBvhNodeTex);
            gRtShader->setInt("uBvhNodes", 1);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_BUFFER, gBvhTriTex);
            gRtShader->setInt("uBvhTris", 2);

            // Fullscreen triangle
            glBindVertexArray(gFsVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            // --- Present pass ---
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, fbw, fbh);
            gPresentShader->use();

            // SVGF params from RenderParams
            gPresentShader->setFloat("uVarMax", gParams.svgfVarMax);
            gPresentShader->setFloat("uKVar", gParams.svgfKVar);
            gPresentShader->setFloat("uKColor", gParams.svgfKColor);
            gPresentShader->setFloat("uKVarMotion", gParams.svgfKVarMotion);
            gPresentShader->setFloat("uKColorMotion", gParams.svgfKColorMotion);
            gPresentShader->setFloat("uSvgfStrength", gParams.svgfStrength);
            gPresentShader->setFloat("uSvgfVarStaticEps", gParams.svgfVarEPS);
            gPresentShader->setFloat("uSvgfMotionStaticEps", gParams.svgfMotionEPS);
            gPresentShader->setInt("uEnableSVGF", gParams.enableSVGF);

            // COLOR0 (accumulated linear + M2 in A)
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gAccum.writeTex());
            gPresentShader->setInt("uTex", 0);
            gPresentShader->setFloat("uExposure", gParams.exposure);

            // COLOR1 (motion vectors, RG16F)
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, gAccum.motionTex);
            gPresentShader->setInt("uMotionTex", 1);
            gPresentShader->setInt("uShowMotion", gShowMotion ? 1 : 0);
            gPresentShader->setFloat("uMotionScale", gParams.motionScale);
            gPresentShader->setVec2("uResolution", glm::vec2(fbw, fbh));

            // GBUFFER: world-space position
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, gGBuffer.posTex);
            gPresentShader->setInt("uGPos", 2);

            // GBUFFER: world-space normal
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, gGBuffer.nrmTex);
            gPresentShader->setInt("uGNrm", 3);

            glBindVertexArray(gFsVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            // Advance accumulation & ping-pong
            gAccum.swapAfterFrame();
        } else {
            // ===============================
            // Raster path (FIXED)
            // ===============================

            // ALWAYS bind back to default framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            // Restore normal pipeline state
            glDisable(GL_SCISSOR_TEST); // Ray mode had this enabled
            glViewport(0, 0, fbw, fbh);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);

            // MUST CLEAR depth, or rasterization draws nothing
            glClearColor(0.1f, 0.0f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Use raster shader *after* framebuffer state is correct
            gRasterShader->use();

            // Camera matrices
            gRasterShader->setMat4("view", currView);
            gRasterShader->setMat4("projection", currProj);

            // --- Draw objects ---

            auto model = glm::mat4(1.0f);
            gRasterShader->setMat4("model", model);
            gRasterShader->setVec3("uColor", glm::vec3(0.1f, 0.4f, 0.1f));
            gGround->Draw();

            model = glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 1.5f, 0.0f));
            model = glm::scale(model, glm::vec3(0.5f));
            gRasterShader->setMat4("model", model);
            gRasterShader->setVec3("uColor", glm::vec3(0.9f));
            gBunny->Draw();

            model = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(0.5f));
            gRasterShader->setMat4("model", model);
            gRasterShader->setVec3("uColor", glm::vec3(0.3f, 0.6f, 1.0f));
            gSphere->Draw();
        }

        // ---- End-of-frame: promote curr -> prev for next frame's motion/TAA ----
        gFrame.endFrame();

        // ---- GUI: draw control panel on top of final image ----
        ui::Draw(gParams, gFrame, gInput, gRayMode, gUseBVH, gShowMotion, gBvhPicker);
        ui::EndFrame();

        // If the BVH picker requested a reload, rebuild from the chosen path
        if (gBvhPicker.reloadRequested) {
            gBvhPicker.reloadRequested = false;
            rebuildBVHFromModelPath(gBvhPicker.currentPath);
            gAccum.reset(); // new geometry → reset accumulation
        }

        // ---- Detect GUI-driven changes and reset accumulation if needed ----
        bool guiChangedMode =
                (gRayMode != prevRayMode) ||
                (gUseBVH != prevUseBVH) ||
                (gShowMotion != prevShowMotion);

        auto diff = [](const float a, const float b) {
            return std::fabs(a - b) > 1e-7f;
        };

        bool guiChangedParams = false;

        // Integers / bools that affect sampling / shading
        if (gParams.sppPerFrame != prevGuiParams.sppPerFrame) guiChangedParams = true;
        if (gParams.enableGI != prevGuiParams.enableGI) guiChangedParams = true;
        if (gParams.enableAO != prevGuiParams.enableAO) guiChangedParams = true;
        if (gParams.enableMirror != prevGuiParams.enableMirror)guiChangedParams = true;
        if (gParams.enableTAA != prevGuiParams.enableTAA) guiChangedParams = true;
        if (gParams.enableSVGF != prevGuiParams.enableSVGF) guiChangedParams = true;
        if (gParams.aoSamples != prevGuiParams.aoSamples) guiChangedParams = true;

        // Jitter
        if (gParams.enableJitter != prevGuiParams.enableJitter) guiChangedParams = true;
        if (diff(gParams.jitterStillScale, prevGuiParams.jitterStillScale)) guiChangedParams = true;
        if (diff(gParams.jitterMovingScale, prevGuiParams.jitterMovingScale)) guiChangedParams = true;

        // Floats that affect the integrand / filter behavior
        if (diff(gParams.giScaleAnalytic, prevGuiParams.giScaleAnalytic)) guiChangedParams = true;
        if (diff(gParams.giScaleBVH, prevGuiParams.giScaleBVH)) guiChangedParams = true;
        if (diff(gParams.mirrorStrength, prevGuiParams.mirrorStrength)) guiChangedParams = true;
        if (diff(gParams.aoRadius, prevGuiParams.aoRadius)) guiChangedParams = true;
        if (diff(gParams.aoBias, prevGuiParams.aoBias)) guiChangedParams = true;
        if (diff(gParams.aoMin, prevGuiParams.aoMin)) guiChangedParams = true;

        if (diff(gParams.taaStillThresh, prevGuiParams.taaStillThresh)) guiChangedParams = true;
        if (diff(gParams.taaHardMovingThresh, prevGuiParams.taaHardMovingThresh)) guiChangedParams = true;
        if (diff(gParams.taaHistoryMinWeight, prevGuiParams.taaHistoryMinWeight)) guiChangedParams = true;
        if (diff(gParams.taaHistoryAvgWeight, prevGuiParams.taaHistoryAvgWeight)) guiChangedParams = true;
        if (diff(gParams.taaHistoryMaxWeight, prevGuiParams.taaHistoryMaxWeight)) guiChangedParams = true;
        if (diff(gParams.taaHistoryBoxSize, prevGuiParams.taaHistoryBoxSize)) guiChangedParams = true;

        if (diff(gParams.svgfStrength, prevGuiParams.svgfStrength)) guiChangedParams = true;
        if (diff(gParams.svgfVarMax, prevGuiParams.svgfVarMax)) guiChangedParams = true;
        if (diff(gParams.svgfKVar, prevGuiParams.svgfKVar)) guiChangedParams = true;
        if (diff(gParams.svgfKColor, prevGuiParams.svgfKColor)) guiChangedParams = true;
        if (diff(gParams.svgfKVarMotion, prevGuiParams.svgfKVarMotion)) guiChangedParams = true;
        if (diff(gParams.svgfKColorMotion, prevGuiParams.svgfKColorMotion)) guiChangedParams = true;
        if (diff(gParams.svgfVarEPS, prevGuiParams.svgfVarEPS)) guiChangedParams = true;
        if (diff(gParams.svgfMotionEPS, prevGuiParams.svgfMotionEPS)) guiChangedParams = true;

        // Extra explicit logs for TAA / SVGF toggles via GUI
        if (gParams.enableTAA != prevGuiParams.enableTAA) {
            ui::Log("[TAA] %s\n", gParams.enableTAA ? "ENABLED" : "DISABLED");
        }
        if (gParams.enableSVGF != prevGuiParams.enableSVGF) {
            ui::Log("[SVGF] %s\n", gParams.enableSVGF ? "ENABLED" : "DISABLED");
        }

        // exposure and motionScale are presentation-only → no history reset needed
        if (guiChangedMode || guiChangedParams) {
            gAccum.reset();
            ui::Log("[ACCUM] Reset due to GUI changes (%s%s)\n",
                    guiChangedMode ? "mode " : "",
                    guiChangedParams ? "params" : "");
        }

        glfwSwapBuffers(window);
    }

    // ===============================
    // Cleanup
    // ===============================
    delete gRtShader;
    gRtShader = nullptr;
    delete gPresentShader;
    gPresentShader = nullptr;
    delete gRasterShader;
    gRasterShader = nullptr;

    delete gGround;
    gGround = nullptr;
    delete gBunny;
    gBunny = nullptr;
    delete gSphere;
    gSphere = nullptr;
    delete gBvhModel;
    gBvhModel = nullptr;

    if (gFsVao) {
        glDeleteVertexArrays(1, &gFsVao);
        gFsVao = 0;
    }
    if (gBvhNodeTex) {
        glDeleteTextures(1, &gBvhNodeTex);
        gBvhNodeTex = 0;
    }
    if (gBvhTriTex) {
        glDeleteTextures(1, &gBvhTriTex);
        gBvhTriTex = 0;
    }
    if (gBvhNodeBuf) {
        glDeleteBuffers(1, &gBvhNodeBuf);
        gBvhNodeBuf = 0;
    }
    if (gBvhTriBuf) {
        glDeleteBuffers(1, &gBvhTriBuf);
        gBvhTriBuf = 0;
    }

    // ImGui shutdown
    ui::Shutdown();

    glfwTerminate();
    return 0;
}
