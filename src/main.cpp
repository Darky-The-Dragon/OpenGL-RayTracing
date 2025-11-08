#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "Camera.h"
#include "Shader.h"
#include "utils/model.h"
#include "bvh.h" // NEW
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>

// ===============================
// Ray mode & accumulation state
// ===============================
static bool gRayMode = true; // F2: raster <-> ray
static int gFrameIndex = 0;
static bool gPrevF2 = false, gPrevR = false, gPrevF5 = false;

// SPP & exposure (you already had these)
static int gSppPerFrame = 1;
static float gExposure = 1.0f;

// Ping-pong accumulation targets
static GLuint gAccumTex[2] = {0, 0};
static GLuint gAccumFbo = 0;
static int gWriteIdx = 0;
static GLuint gFsVao = 0;

// Shaders
static Shader *gRtShader = nullptr;
static Shader *gPresentShader = nullptr;

// Camera
float lastX = 400, lastY = 300;
float deltaTime = 0.0f, lastFrame = 0.0f;
bool firstMouse = true;
Camera camera(glm::vec3(0.0f, 2.0f, 8.0f), -90.0f, -10.0f, 60.0f, 800.0f / 600.0f);

// Raster path (unchanged)
static Shader *gRasterShader = nullptr;
static Model *gGround = nullptr;
static Model *gBunny = nullptr;
static Model *gSphere = nullptr;

// BVH data & toggle
static bool gUseBVH = false; // F5 toggles this
static GLuint gBvhNodeTex = 0, gBvhNodeBuf = 0;
static GLuint gBvhTriTex = 0, gBvhTriBuf = 0;
static int gBvhNodeCount = 0, gBvhTriCount = 0;

// ===============================
static GLuint createAccumTex(int w, int h) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void createAccumTargets(int w, int h) {
    if (!gAccumFbo)
        glGenFramebuffers(1, &gAccumFbo);
    if (gAccumTex[0])
        glDeleteTextures(1, &gAccumTex[0]);
    if (gAccumTex[1])
        glDeleteTextures(1, &gAccumTex[1]);
    gAccumTex[0] = createAccumTex(w, h);
    gAccumTex[1] = createAccumTex(w, h);
    gWriteIdx = 0;
    gFrameIndex = 0;
}

// ===============================
// Callbacks
static void framebuffer_size_callback(GLFWwindow *, int width, int height) {
    glViewport(0, 0, width, height);
    if (height > 0) camera.AspectRatio = float(width) / float(height);
    createAccumTargets(width, height);
}

static void mouse_callback(GLFWwindow *, double xpos, double ypos) {
    if (firstMouse) {
        lastX = float(xpos);
        lastY = float(ypos);
        firstMouse = false;
    }
    float dx = float(xpos) - lastX, dy = lastY - float(ypos);
    lastX = float(xpos);
    lastY = float(ypos);
    camera.ProcessMouseMovement(dx, dy);
    gFrameIndex = 0;
}

static void scroll_callback(GLFWwindow *, double, double yoff) {
    camera.Fov -= float(yoff) * 2.0f;
    camera.Fov = glm::clamp(camera.Fov, 20.0f, 90.0f);
    gFrameIndex = 0;
}

// ===============================
int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);
#endif
    GLFWwindow *window = glfwCreateWindow(800, 600, "OpenGL Ray/Path Tracing - Darky", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    gladLoadGL(glfwGetProcAddress);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    glEnable(GL_DEPTH_TEST);
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;

    // Shaders
    gRtShader = new Shader("../shaders/rt_fullscreen.vert", "../shaders/rt.frag");
    gPresentShader = new Shader("../shaders/rt_fullscreen.vert", "../shaders/rt_present.frag");

    int fbw, fbh;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    createAccumTargets(fbw, fbh);
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
    // transform: center a bit and scale
    glm::mat4 M = glm::mat4(1.0f);
    M = glm::translate(M, glm::vec3(0.0f, 1.0f, -3.0f));
    M = glm::scale(M, glm::vec3(1.0f));
    gather_model_triangles(*gBunny, M, triCPU);

    std::vector<BVHNode> nodesCPU = build_bvh(triCPU);
    gBvhNodeCount = (int) nodesCPU.size();
    gBvhTriCount = (int) triCPU.size();

    upload_bvh_tbos(nodesCPU, triCPU,
                    gBvhNodeTex, gBvhNodeBuf,
                    gBvhTriTex, gBvhTriBuf);

    // Fullscreen triangle VAO (no VBO)
    // (already created)

    glm::vec3 prevPos = camera.Position;
    float prevFov = camera.Fov;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // ESC
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        float tnow = float(glfwGetTime());
        deltaTime = tnow - lastFrame;
        lastFrame = tnow;

        camera.ProcessKeyboardInput(window, deltaTime);

        // Toggles
        bool nowF2 = (glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS);
        if (nowF2 && !gPrevF2) {
            gRayMode = !gRayMode;
            gFrameIndex = 0;
        }
        gPrevF2 = nowF2;

        bool nowR = (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS);
        if (nowR && !gPrevR) { gFrameIndex = 0; }
        gPrevR = nowR;

        bool nowF5 = (glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS);
        if (nowF5 && !gPrevF5) {
            gUseBVH = !gUseBVH;
            gFrameIndex = 0;
            std::cout << "[SCENE] BVH mode " << (gUseBVH ? "ON" : "OFF")
                    << "  (nodes=" << gBvhNodeCount << ", tris=" << gBvhTriCount << ")\n";
        }
        gPrevF5 = nowF5;

        // ---- Input: SPP (↑/↓) and Exposure ([ / ]) with simple debounce ----
        static float keyRepeat = 0.0f;
        keyRepeat += deltaTime;
        const float repeatStep = 0.10f; // seconds between repeats while holding

        if (keyRepeat >= repeatStep) {
            // Increase SPP per frame (1 -> 2 -> 4 -> 8 -> 16)
            if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
                int old = gSppPerFrame;
                if      (gSppPerFrame < 2)  gSppPerFrame = 2;
                else if (gSppPerFrame < 4)  gSppPerFrame = 4;
                else if (gSppPerFrame < 8)  gSppPerFrame = 8;
                else if (gSppPerFrame < 16) gSppPerFrame = 16;
                if (gSppPerFrame != old) {
                    gFrameIndex = 0; // restart accumulation for new SPP
                    std::cout << "[INPUT] SPP per frame = " << gSppPerFrame << "\n";
                    keyRepeat = 0.0f;
                }
            }
            // Decrease SPP per frame (16 -> 8 -> 4 -> 2 -> 1)
            if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
                int old = gSppPerFrame;
                if      (gSppPerFrame > 8)  gSppPerFrame = 8;
                else if (gSppPerFrame > 4)  gSppPerFrame = 4;
                else if (gSppPerFrame > 2)  gSppPerFrame = 2;
                else                        gSppPerFrame = 1;
                if (gSppPerFrame != old) {
                    gFrameIndex = 0; // restart accumulation for new SPP
                    std::cout << "[INPUT] SPP per frame = " << gSppPerFrame << "\n";
                    keyRepeat = 0.0f;
                }
            }
            // Exposure down: '['
            if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS) {
                float old = gExposure;
                gExposure = std::max(0.05f, gExposure * 0.97f);
                if (gExposure != old) {
                    std::cout << "[INPUT] Exposure = " << gExposure << "\n";
                    keyRepeat = 0.0f;
                }
            }
            // Exposure up: ']'
            if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS) {
                float old = gExposure;
                gExposure = std::min(8.0f, gExposure * 1.03f);
                if (gExposure != old) {
                    std::cout << "[INPUT] Exposure = " << gExposure << "\n";
                    keyRepeat = 0.0f;
                }
            }
        }

        if (glm::distance(prevPos, camera.Position) > 1e-6f || std::fabs(prevFov - camera.Fov) > 1e-6f) {
            gFrameIndex = 0;
            prevPos = camera.Position;
            prevFov = camera.Fov;
        }

        glClearColor(0.1f, 0.0f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glfwGetFramebufferSize(window, &fbw, &fbh);

        if (gRayMode) {
            glBindFramebuffer(GL_FRAMEBUFFER, gAccumFbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gAccumTex[gWriteIdx], 0);
            static const GLenum bufs[1] = {GL_COLOR_ATTACHMENT0};
            glDrawBuffers(1, bufs);
            glViewport(0, 0, fbw, fbh);
            glDisable(GL_DEPTH_TEST);

            gRtShader->use();

            // Camera basis
            glm::mat4 V = camera.GetViewMatrix();
            glm::vec3 right = glm::normalize(glm::vec3(V[0][0], V[1][0], V[2][0]));
            glm::vec3 up = glm::normalize(glm::vec3(V[0][1], V[1][1], V[2][1]));
            glm::vec3 fwd = -glm::normalize(glm::vec3(V[0][2], V[1][2], V[2][2]));
            float tanHalfFov = std::tanf(glm::radians(camera.Fov) * 0.5f);

            gRtShader->setVec3("uCamPos", camera.Position);
            gRtShader->setVec3("uCamRight", right);
            gRtShader->setVec3("uCamUp", up);
            gRtShader->setVec3("uCamFwd", fwd);
            gRtShader->setFloat("uTanHalfFov", tanHalfFov);
            gRtShader->setFloat("uAspect", camera.AspectRatio);
            gRtShader->setInt("uFrameIndex", gFrameIndex);
            gRtShader->setInt("uSpp", gSppPerFrame);
            gRtShader->setInt("uUseBVH", gUseBVH ? 1 : 0);
            gRtShader->setInt("uNodeCount", gBvhNodeCount);
            gRtShader->setInt("uTriCount", gBvhTriCount);

            if (GLint locRes = glGetUniformLocation(gRtShader->ID, "uResolution"); locRes >= 0)
                glUniform2f(locRes, float(fbw), float(fbh));

            // Bind accumulation read
            int readIdx = 1 - gWriteIdx;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gAccumTex[readIdx]);
            gRtShader->setInt("uPrevAccum", 0);

            // Bind BVH TBOs (only used if uUseBVH==1)
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
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gAccumTex[gWriteIdx]);
            gPresentShader->setInt("uTex", 0);
            gPresentShader->setFloat("uExposure", gExposure);
            glBindVertexArray(gFsVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            gFrameIndex++;
            gWriteIdx = readIdx;
        } else {
            // Raster path (unchanged)
            glEnable(GL_DEPTH_TEST);
            gRasterShader->use();
            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 proj = camera.GetProjectionMatrix();
            gRasterShader->setMat4("view", view);
            gRasterShader->setMat4("projection", proj);

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
    if (gAccumTex[0])
        glDeleteTextures(1, &gAccumTex[0]);
    if (gAccumTex[1])
        glDeleteTextures(1, &gAccumTex[1]);
    if (gAccumFbo)
        glDeleteFramebuffers(1, &gAccumFbo);

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
