#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "../include/camera/Camera.h"
#include "Shader.h"
#include "utils/model.h"
#include "rt/accum.h"
#include "bvh.h"
#include "io/input.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>

// ===============================
// Global state
// ===============================
static rt::Accum gAccum; // accumulation ping-pong + frame counter
static bool gRayMode = true; // F2: raster <-> ray

// SPP & exposure (driven by io::InputState)
static int gSppPerFrame = 1;
static float gExposure = 1.0f;

// Motion debug (F6) – local toggle only
static bool gShowMotion = false;
static bool gPrevF6 = false;

// Fullscreen triangle
static GLuint gFsVao = 0;

// Shaders
static Shader *gRtShader = nullptr;
static Shader *gPresentShader = nullptr;

// Camera
static float deltaTime = 0.0f, lastFrame = 0.0f;
static Camera camera(glm::vec3(0.0f, 2.0f, 8.0f), -90.0f, -10.0f, 60.0f, 800.0f / 600.0f);

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

// Matrices for motion vectors
static glm::mat4 gPrevViewProj(1.0f);

// ===============================
// Callbacks
// ===============================
static void framebuffer_size_callback(GLFWwindow *, int width, int height) {
    glViewport(0, 0, width, height);
    if (height > 0) camera.AspectRatio = float(width) / float(height);
    gAccum.recreate(width, height); // rebuild accumulation targets + reset counters
}

// ===============================
int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow *window = glfwCreateWindow(800, 600, "OpenGL Ray/Path Tracing - Darky", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    gladLoadGL(glfwGetProcAddress);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // route mouse + scroll to io module (resets accumulation inside)
    io::attach_callbacks(window, &camera, &gAccum.frameIndex, &gInput);

    glEnable(GL_DEPTH_TEST);
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;

    // Shaders
    gRtShader = new Shader("../shaders/rt_fullscreen.vert", "../shaders/rt.frag");
    gPresentShader = new Shader("../shaders/rt_fullscreen.vert", "../shaders/rt_present.frag");

    int fbw, fbh;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    gAccum.recreate(fbw, fbh);

    // Fullscreen triangle VAO (no VBO)
    glGenVertexArrays(1, &gFsVao);

    // Raster scene (unchanged)
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
    gBvhNodeCount = (int) nodesCPU.size();
    gBvhTriCount = (int) triCPU.size();

    upload_bvh_tbos(nodesCPU, triCPU,
                    gBvhNodeTex, gBvhNodeBuf,
                    gBvhTriTex, gBvhTriBuf);

    // Input init
    gInput.sppPerFrame = gSppPerFrame;
    gInput.exposure = gExposure;
    io::init(gInput);

    // Initialize prev VP to current so motion starts ~zero
    gPrevViewProj = camera.GetProjectionMatrix() * camera.GetViewMatrix();

    glm::vec3 prevPos = camera.Position;
    float prevFov = camera.Fov;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Time
        float tnow = float(glfwGetTime());
        deltaTime = tnow - lastFrame;
        lastFrame = tnow;

        // Camera movement
        camera.ProcessKeyboardInput(window, deltaTime);

        // Centralized input update
        const bool anyChanged = io::update(gInput, window, deltaTime);

        // ESC
        if (gInput.quitRequested) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // Local F6 toggle for motion debug (doesn't touch InputState)
        const bool nowF6 = (glfwGetKey(window, GLFW_KEY_F6) == GLFW_PRESS);
        if (nowF6 && !gPrevF6) {
            gShowMotion = !gShowMotion;
            gAccum.reset();
            std::cout << "[DEBUG] Show Motion = " << (gShowMotion ? "ON" : "OFF") << "\n";
        }
        gPrevF6 = nowF6;

        // Apply input events
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
                std::cout << "[SCENE] BVH mode " << (gUseBVH ? "ON" : "OFF")
                        << "  (nodes=" << gBvhNodeCount << ", tris=" << gBvhTriCount << ")\n";
            }
            if (gInput.changedSPP) {
                gSppPerFrame = gInput.sppPerFrame;
                gAccum.reset();
                std::cout << "[INPUT] SPP per frame = " << gSppPerFrame << "\n";
            }
            if (gExposure != gInput.exposure) {
                gExposure = gInput.exposure;
                std::cout << "[INPUT] Exposure = " << gExposure << "\n";
            }
        }

        // Reset accumulation on camera/FOV change
        if (glm::distance(prevPos, camera.Position) > 1e-6f ||
            std::fabs(prevFov - camera.Fov) > 1e-6f) {
            gAccum.reset();
            prevPos = camera.Position;
            prevFov = camera.Fov;
        }

        // Clear
        glClearColor(0.1f, 0.0f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glfwGetFramebufferSize(window, &fbw, &fbh);

        // Precompute matrices for motion vectors
        const glm::mat4 currView = camera.GetViewMatrix();
        const glm::mat4 currProj = camera.GetProjectionMatrix();
        const glm::mat4 currVP = currProj * currView;

        if (gRayMode) {
            // ===============================
            // Ray mode: render into accum tex + motion
            // ===============================
            gAccum.bindWriteFBO_ColorAndMotion(); // attaches COLOR0 = writeTex(), COLOR1 = motionTex (RG16F)
            glViewport(0, 0, fbw, fbh);
            glDisable(GL_DEPTH_TEST);

            gRtShader->use();

            // Camera basis
            glm::vec3 right = glm::normalize(glm::vec3(currView[0][0], currView[1][0], currView[2][0]));
            glm::vec3 up = glm::normalize(glm::vec3(currView[0][1], currView[1][1], currView[2][1]));
            glm::vec3 fwd = -glm::normalize(glm::vec3(currView[0][2], currView[1][2], currView[2][2]));
            float tanHalfFov = std::tanf(glm::radians(camera.Fov) * 0.5f);

            gRtShader->setVec3("uCamPos", camera.Position);
            gRtShader->setVec3("uCamRight", right);
            gRtShader->setVec3("uCamUp", up);
            gRtShader->setVec3("uCamFwd", fwd);
            gRtShader->setFloat("uTanHalfFov", tanHalfFov);
            gRtShader->setFloat("uAspect", camera.AspectRatio);
            gRtShader->setInt("uFrameIndex", gAccum.frameIndex);
            gRtShader->setInt("uSpp", gShowMotion ? 1 : gSppPerFrame);
            gRtShader->setInt("uUseBVH", gUseBVH ? 1 : 0);
            gRtShader->setInt("uNodeCount", gBvhNodeCount);
            gRtShader->setInt("uTriCount", gBvhTriCount);
            gRtShader->setInt("uShowMotion", gShowMotion ? 1 : 0);

            // Motion matrices
            gRtShader->setMat4("uPrevViewProj", gPrevViewProj);
            gRtShader->setMat4("uCurrViewProj", currVP);

            if (GLint locRes = glGetUniformLocation(gRtShader->ID, "uResolution"); locRes >= 0)
                glUniform2f(locRes, float(fbw), float(fbh));

            // Accum input
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

            glBindVertexArray(gFsVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            // Present
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, fbw, fbh);
            gPresentShader->use();

            // COLOR0 (accumulated linear)
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gAccum.writeTex());
            gPresentShader->setInt("uTex", 0);
            gPresentShader->setFloat("uExposure", gExposure);

            // COLOR1 (motion vectors, RG16F)
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, gAccum.motionTex); // <- ensure Accum exposes motionTex
            gPresentShader->setInt("uMotionTex", 1);
            gPresentShader->setInt("uShowMotion", gShowMotion ? 1 : 0);
            gPresentShader->setFloat("uMotionScale", 4.0f);

            glBindVertexArray(gFsVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            // Advance accumulation & ping-pong
            gAccum.swapAfterFrame();

            // After presenting, current becomes previous for next frame motion
            gPrevViewProj = currVP;
        } else {
            // ===============================
            // Raster path (unchanged)
            // ===============================
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

        glfwSwapBuffers(window);
    }

    // Cleanup
    if (gFsVao)
        glDeleteVertexArrays(1, &gFsVao);

    if (gBvhNodeTex)
        glDeleteTextures(1, &gBvhNodeTex);
    if (gBvhTriTex)
        glDeleteTextures(1, &gBvhTriTex);
    if (gBvhNodeBuf)
        glDeleteBuffers(1, &gBvhNodeBuf);
    if (gBvhTriBuf)
        glDeleteBuffers(1, &gBvhTriBuf);

    delete gRtShader;
    delete gPresentShader;
    delete gRasterShader;
    delete gGround;
    delete gBunny;
    delete gSphere;

    glfwTerminate();
    return 0;
}
