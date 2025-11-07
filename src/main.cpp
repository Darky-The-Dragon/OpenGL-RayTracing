#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "Camera.h"
#include "Shader.h"
#include "utils/model.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>

// ===============================
// Ray mode & accumulation state
// ===============================
static bool gRayMode = true; // F2 toggles; start in ray mode
static int gFrameIndex = 0; // reset to 0 on any camera/scene change
static bool gPrevF2 = false, gPrevR = false, gPrevF3 = false;

// Per-frame sampling
static int gSppPerFrame = 1; // 1/2/4/8 via hotkeys or F3 cycle

// Exposure (for tonemapper)
static float gExposure = 1.0f;

// Ping-pong accumulation targets
static GLuint gAccumTex[2] = {0, 0};
static GLuint gAccumFbo = 0;
static int gWriteIdx = 0;
static GLuint gFsVao = 0; // fullscreen triangle VAO

// Shaders for ray pass and presentation
static Shader *gRtShader = nullptr; // ../shaders/rt_fullscreen.vert + ../shaders/rt.frag
static Shader *gPresentShader = nullptr; // ../shaders/rt_fullscreen.vert + ../shaders/rt_present.frag

// ===============================
// Camera globals
// ===============================
float lastX = 400, lastY = 300;
float deltaTime = 0.0f;
float lastFrame = 0.0f;
bool firstMouse = true;
Camera camera(glm::vec3(0.0f, 2.0f, 8.0f), -90.0f, -10.0f, 60.0f, 800.0f / 600.0f);

// ===============================
// Helpers
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
// ===============================
void framebuffer_size_callback(GLFWwindow * /*window*/, const int width, const int height) {
    glViewport(0, 0, width, height);
    if (height > 0) camera.AspectRatio = static_cast<float>(width) / static_cast<float>(height);
    createAccumTargets(width, height); // rebuild ping-pong + reset frame index
}

void mouse_callback(GLFWwindow * /*window*/, const double xpos, const double ypos) {
    if (firstMouse) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
    }
    float xoffset = static_cast<float>(xpos) - lastX;
    float yoffset = lastY - static_cast<float>(ypos);
    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);
    camera.ProcessMouseMovement(xoffset, yoffset);
    gFrameIndex = 0; // camera changed → reset accumulation
}

void scroll_callback(GLFWwindow * /*window*/, double /*xoffset*/, const double yoffset) {
    camera.Fov -= static_cast<float>(yoffset) * 2.0f;
    if (camera.Fov < 20.0f) camera.Fov = 20.0f;
    if (camera.Fov > 90.0f) camera.Fov = 90.0f;
    gFrameIndex = 0; // FOV changed → reset accumulation
}

// ===============================
// Main
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
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    glEnable(GL_DEPTH_TEST);
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;

    // --- RT shaders & targets
    gRtShader = new Shader("../shaders/rt_fullscreen.vert", "../shaders/rt.frag");
    gPresentShader = new Shader("../shaders/rt_fullscreen.vert", "../shaders/rt_present.frag");

    int fbw, fbh;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    createAccumTargets(fbw, fbh);

    // Fullscreen triangle VAO (no VBO)
    glGenVertexArrays(1, &gFsVao);

    // --- Raster shader + models (your existing path)
    const Shader shader("../shaders/basic.vert", "../shaders/basic.frag");
    const Model ground("../models/plane.obj");
    const Model bunny("../models/bunny_lp.obj");
    const Model sphere("../models/sphere.obj");
    if (ground.meshes.empty()) std::cerr << "Failed to load plane.obj\n";
    if (bunny.meshes.empty()) std::cerr << "Failed to load bunny_lp.obj\n";
    if (sphere.meshes.empty()) std::cerr << "Failed to load sphere.obj\n";

    // For accumulation reset
    glm::vec3 prevPos = camera.Position;
    float prevFov = camera.Fov;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // ESC to quit
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        const auto currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        camera.ProcessKeyboardInput(window, deltaTime);

        // Edge-triggered toggles
        const bool nowF2 = glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS;
        if (nowF2 && !gPrevF2) {
            gRayMode = !gRayMode;
            gFrameIndex = 0;
        }
        gPrevF2 = nowF2;

        const bool nowR = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
        if (nowR && !gPrevR) { gFrameIndex = 0; }
        gPrevR = nowR;

        // SPP hotkeys: 1/2/3/4 set to 1/2/4/8; F3 cycles
        if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
            gSppPerFrame = 2;
            gFrameIndex = 0;
        }
        if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
            gSppPerFrame = 4;
            gFrameIndex = 0;
        }
        if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) {
            gSppPerFrame = 8;
            gFrameIndex = 0;
        }
        if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) {
            gSppPerFrame = 16;
            gFrameIndex = 0;
        }
        const bool nowF3 = glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS;
        if (nowF3 && !gPrevF3) {
            gSppPerFrame = (gSppPerFrame == 2) ? 4 : (gSppPerFrame == 4) ? 8 : (gSppPerFrame == 8) ? 16 : 1;
            gFrameIndex = 0;
        }
        gPrevF3 = nowF3;

        // Exposure hotkeys: [ to decrease, ] to increase (clamped)
        if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS) { gExposure = std::max(0.05f, gExposure * 0.97f); }
        if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS) { gExposure = std::min(8.0f, gExposure * 1.03f); }

        // Detect camera change and reset accumulation
        if (glm::distance(prevPos, camera.Position) > 1e-6f || std::fabs(prevFov - camera.Fov) > 1e-6f) {
            gFrameIndex = 0;
            prevPos = camera.Position;
            prevFov = camera.Fov;
        }

        glClearColor(0.1f, 0.0f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glfwGetFramebufferSize(window, &fbw, &fbh);

        if (gRayMode) {
            // ===============================
            // Ray mode: render into accum tex
            // ===============================
            glBindFramebuffer(GL_FRAMEBUFFER, gAccumFbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gAccumTex[gWriteIdx], 0);
            static constexpr GLenum bufs[1] = {GL_COLOR_ATTACHMENT0};
            glDrawBuffers(1, bufs); // ensure draw to color 0

            // Optional: sanity check
            const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "FBO incomplete: 0x" << std::hex << status << std::dec << "\n";
            }

            glViewport(0, 0, fbw, fbh);
            glDisable(GL_DEPTH_TEST);

            gRtShader->use();

            // Camera basis
            glm::mat4 V = camera.GetViewMatrix();
            glm::vec3 right = glm::normalize(glm::vec3(V[0][0], V[1][0], V[2][0]));
            glm::vec3 up = glm::normalize(glm::vec3(V[0][1], V[1][1], V[2][1]));
            glm::vec3 fwd = -glm::normalize(glm::vec3(V[0][2], V[1][2], V[2][2])); // note the minus
            const float tanHalfFov = std::tanf(glm::radians(camera.Fov) * 0.5f);

            gRtShader->setVec3("uCamPos", camera.Position);
            gRtShader->setVec3("uCamRight", right);
            gRtShader->setVec3("uCamUp", up);
            gRtShader->setVec3("uCamFwd", fwd);
            gRtShader->setFloat("uTanHalfFov", tanHalfFov);
            gRtShader->setFloat("uAspect", camera.AspectRatio);
            gRtShader->setInt("uFrameIndex", gFrameIndex);
            gRtShader->setInt("uSpp", gSppPerFrame);

            if (const GLint locRes = glGetUniformLocation(gRtShader->ID, "uResolution"); locRes >= 0)
                glUniform2f(locRes, static_cast<float>(fbw), static_cast<float>(fbh));

            const int readIdx = 1 - gWriteIdx;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gAccumTex[readIdx]);
            gRtShader->setInt("uPrevAccum", 0);

            glBindVertexArray(gFsVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            // Present to default framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, fbw, fbh);
            gPresentShader->use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gAccumTex[gWriteIdx]);
            gPresentShader->setInt("uTex", 0);
            gPresentShader->setFloat("uExposure", gExposure); // NEW
            glBindVertexArray(gFsVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            // Advance accumulation & ping-pong
            gFrameIndex++;
            gWriteIdx = readIdx;
        } else {
            // ===============================
            // Raster mode: your original path
            // ===============================
            glEnable(GL_DEPTH_TEST);

            shader.use();
            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 projection = camera.GetProjectionMatrix();
            shader.setMat4("view", view);
            shader.setMat4("projection", projection);

            // Ground
            auto model = glm::mat4(1.0f);
            shader.setMat4("model", model);
            shader.setVec3("uColor", glm::vec3(0.1f, 0.4f, 0.1f)); // green
            ground.Draw();

            // Bunny
            model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(-2.0f, 1.5f, 0.0f));
            model = glm::scale(model, glm::vec3(0.5f));
            shader.setMat4("model", model);
            shader.setVec3("uColor", glm::vec3(0.9f, 0.9f, 0.9f)); // white
            bunny.Draw();

            // Sphere
            model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(2.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(0.5f));
            shader.setMat4("model", model);
            shader.setVec3("uColor", glm::vec3(0.3f, 0.6f, 1.0f)); // blue
            sphere.Draw();
        }

        glfwSwapBuffers(window);
    }

    // Basic cleanup
    if (gFsVao)
        glDeleteVertexArrays(1, &gFsVao);
    if (gAccumTex[0])
        glDeleteTextures(1, &gAccumTex[0]);
    if (gAccumTex[1])
        glDeleteTextures(1, &gAccumTex[1]);
    if (gAccumFbo)
        glDeleteFramebuffers(1, &gAccumFbo);
    delete gRtShader;
    delete gPresentShader;

    glfwTerminate();
    return 0;
}
