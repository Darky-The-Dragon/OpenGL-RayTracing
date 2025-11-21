#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "camera/Camera.h"
#include "Shader.h"
#include "utils/model.h"
#include "rt/accum.h"
#include "rt/RenderParams.h"
#include "rt/frame_state.h"
#include "bvh.h"
#include "io/input.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>

// ===============================
// Global state
// ===============================
static rt::Accum gAccum; // accumulation ping-pong + frame counter
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
    800.0f / 600.0f // aspect
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

// Centralized input state
static io::InputState gInput;

// ===============================
// Callbacks
// ===============================
static void framebuffer_size_callback(GLFWwindow *, int width, int height) {
    if (width <= 0 || height <= 0) return;

    glViewport(0, 0, width, height);
    glScissor(0, 0, width, height);

    camera.AspectRatio = static_cast<float>(width) / static_cast<float>(height);

    gAccum.recreate(width, height);
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
        800, 600,
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

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // Route mouse + scroll to io module (camera only, accumulation controlled in main)
    io::attach_callbacks(window, &camera, &gInput);

    glEnable(GL_DEPTH_TEST);
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;

    // Shaders
    gRtShader = new Shader("../shaders/rt/rt_fullscreen.vert", "../shaders/rt/rt.frag");
    gPresentShader = new Shader("../shaders/rt/rt_fullscreen.vert", "../shaders/rt/rt_present.frag");

    // Accumulation buffers
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    gAccum.recreate(fbw, fbh);

    // Fullscreen triangle VAO (no VBO)
    glGenVertexArrays(1, &gFsVao);

    // Raster scene
    gRasterShader = new Shader("../shaders/basic.vert", "../shaders/basic.frag");
    gGround = new Model("../models/plane.obj");
    gBunny = new Model("../models/bunny_lp.obj");
    gSphere = new Model("../models/sphere.obj");
    if (gGround->meshes.empty()) std::cerr << "Failed to load plane.obj\n";
    if (gBunny->meshes.empty()) std::cerr << "Failed to load bunny_lp.obj\n";
    if (gSphere->meshes.empty()) std::cerr << "Failed to load sphere.obj\n";

    // ---- Build BVH from the bunny (CPU), upload as TBOs ----
    std::vector<CPU_Triangle> triCPU;
    glm::mat4 M(1.0f);
    M = glm::translate(M, glm::vec3(0.0f, 1.0f, -3.0f));
    M = glm::scale(M, glm::vec3(1.0f));
    gather_model_triangles(*gBunny, M, triCPU);

    std::vector<BVHNode> nodesCPU = build_bvh(triCPU);
    gBvhNodeCount = static_cast<int>(nodesCPU.size());
    gBvhTriCount = static_cast<int>(triCPU.size());

    upload_bvh_tbos(
        nodesCPU, triCPU,
        gBvhNodeTex, gBvhNodeBuf,
        gBvhTriTex, gBvhTriBuf
    );

    // --- Input + params init ---
    gInput.sppPerFrame = gParams.sppPerFrame;
    gInput.exposure = gParams.exposure;
    io::init(gInput);

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

        // --- Time update ---
        float tnow = static_cast<float>(glfwGetTime());
        deltaTime = tnow - lastFrame;
        lastFrame = tnow;

        // --- Camera movement (keyboard) ---
        camera.ProcessKeyboardInput(window, deltaTime);

        // --- Centralized input update (SPP, exposure, BVH toggle, etc.) ---
        const bool anyChanged = io::update(gInput, window, deltaTime);

        // ESC → quit
        if (gInput.quitRequested) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
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

        // --- Apply input events that affect accumulation / params ---
        if (anyChanged) {
            if (gInput.toggledRayMode) {
                gRayMode = !gRayMode;
                gAccum.reset();
            }

            if (gInput.resetAccum) {
                gAccum.reset();
            }

            if (gInput.toggledBVH) {
                gUseBVH = !gUseBVH;
                gAccum.reset();
                std::cout << "[SCENE] BVH mode "
                        << (gUseBVH ? "ON" : "OFF")
                        << "  (nodes=" << gBvhNodeCount
                        << ", tris=" << gBvhTriCount << ")\n";
            }

            if (gInput.changedSPP) {
                gParams.sppPerFrame = std::max(1, std::min(gInput.sppPerFrame, 64));
                gAccum.reset();
                std::cout << "[INPUT] SPP per frame = "
                        << gParams.sppPerFrame << "\n";
            }

            if (gParams.exposure != gInput.exposure) {
                gParams.exposure = std::max(0.01f, std::min(gInput.exposure, 8.0f));
                std::cout << "[INPUT] Exposure = "
                        << gParams.exposure << "\n";
            }

            if (gInput.toggledMotionDebug) {
                gShowMotion = !gShowMotion;
                gAccum.reset(); // history invalid when switching debug view
                std::cout << "[DEBUG] Show Motion = "
                          << (gShowMotion ? "ON" : "OFF") << "\n";
            }
        }

        // --- Framebuffer size / viewport / clear ---
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glScissor(0, 0, fbw, fbh);

        glClearColor(0.1f, 0.0f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (gRayMode) {
            // ===============================
            // Ray mode: render into accum tex + motion
            // ===============================
            gAccum.bindWriteFBO_ColorAndMotion();
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
            gRtShader->setInt("uSpp", gShowMotion ? 1 : gParams.sppPerFrame);
            gRtShader->setFloat("uJitterScaleMoving", gParams.jitterScale);

            // Scene / BVH
            gRtShader->setInt("uUseBVH", gUseBVH ? 1 : 0);
            gRtShader->setInt("uNodeCount", gBvhNodeCount);
            gRtShader->setInt("uTriCount", gBvhTriCount);

            // TAA params from RenderParams
            gRtShader->setFloat("uTaaStillThresh",      gParams.taaStillThresh);
            gRtShader->setFloat("uTaaHardMovingThresh", gParams.taaHardMovingThresh);
            gRtShader->setFloat("uTaaHistoryMinWeight", gParams.taaHistoryMinWeight);
            gRtShader->setFloat("uTaaHistoryAvgWeight", gParams.taaHistoryAvgWeight);
            gRtShader->setFloat("uTaaHistoryMaxWeight", gParams.taaHistoryMaxWeight);
            gRtShader->setFloat("uTaaHistoryBoxSize",   gParams.taaHistoryBoxSize);

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
            gPresentShader->setFloat("uKVar",   gParams.svgfKVar);
            gPresentShader->setFloat("uKColor", gParams.svgfKColor);
            gPresentShader->setFloat("uKVarMotion",   gParams.svgfKVarMotion);
            gPresentShader->setFloat("uKColorMotion", gParams.svgfKColorMotion);
            gPresentShader->setFloat("uSvgfStrength",  gParams.svgfStrength);
            gPresentShader->setFloat("uSvgfVarStaticEps",  gParams.svgfVarEPS);
            gPresentShader->setFloat("uSvgfMotionStaticEps",  gParams.svgfMotionEPS);

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

            glBindVertexArray(gFsVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            // Advance accumulation & ping-pong
            gAccum.swapAfterFrame();

        } else {
            // ===============================
            // Raster path (unchanged)
            // ===============================
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
            gRasterShader->use();
            gRasterShader->setMat4("view", currView);
            gRasterShader->setMat4("projection", currProj);

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

    glfwTerminate();
    return 0;
}
