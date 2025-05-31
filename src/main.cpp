#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "Camera.h"
#include "Shader.h"
#include "utils/model.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

// Camera
float lastX = 400, lastY = 300;
float deltaTime = 0.0f;
float lastFrame = 0.0f;
bool firstMouse = true;
Camera camera(glm::vec3(0.0f, 2.0f, 8.0f), -90.0f, -10.0f, 60.0f, 800.0f / 600.0f);

// Resize callback
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// Mouse movement
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }
    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;
    lastX = (float)xpos;
    lastY = (float)ypos;
    camera.ProcessMouseMovement(xoffset, yoffset);
}

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(800, 600, "Test", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    gladLoadGL(glfwGetProcAddress);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glEnable(GL_DEPTH_TEST);

    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;

    Shader shader("../shaders/basic.vert", "../shaders/basic.frag");

    // Load Models
    Model ground("../models/plane.obj");
    Model bunny("../models/bunny_lp.obj");
    Model sphere("../models/sphere.obj");

    if (ground.meshes.empty()) std::cerr << "❌ Failed to load plane.obj\n";
    if (bunny.meshes.empty())  std::cerr << "❌ Failed to load bunny_lp.obj\n";
    if (sphere.meshes.empty()) std::cerr << "❌ Failed to load sphere.obj\n";

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        camera.ProcessKeyboardInput(window, deltaTime);

        glClearColor(0.1f, 0.0f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 projection = camera.GetProjectionMatrix();
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);

        // Ground
        glm::mat4 model = glm::mat4(1.0f);
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

        glfwSwapBuffers(window);
    }

    glfwTerminate();
    return 0;
}
