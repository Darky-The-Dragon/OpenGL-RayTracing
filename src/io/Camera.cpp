#include "../../include/io/Camera.h"
#include <GLFW/glfw3.h>

Camera::Camera(glm::vec3 position, float yaw, float pitch, float fov, float aspectRatio)
    : Position(position),
      Yaw(yaw),
      Pitch(pitch),
      Fov(fov),
      AspectRatio(aspectRatio),
      MovementSpeed(2.5f),
      WorldUp(glm::vec3(0.0f, 1.0f, 0.0f)) {
    UpdateCameraVectors();
}

void Camera::ProcessKeyboardInput(GLFWwindow *window, const float deltaTime) {
    const float velocity = MovementSpeed * deltaTime;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        Position += Front * velocity;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        Position -= Front * velocity;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        Position -= Right * velocity;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        Position += Right * velocity;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        Position += Up * velocity;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        Position -= Up * velocity;
}

void Camera::ProcessMouseMovement(float xOffset, float yOffset) {
    constexpr float sensitivity = 0.1f;
    xOffset *= sensitivity;
    yOffset *= sensitivity;

    Yaw += xOffset;
    Pitch += yOffset;

    // Constrain the pitch
    if (Pitch > 89.0f)
        Pitch = 89.0f;
    if (Pitch < -89.0f)
        Pitch = -89.0f;

    UpdateCameraVectors();
}

void Camera::UpdateCameraVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    front.y = sin(glm::radians(Pitch));
    front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    Front = glm::normalize(front);

    Right = glm::normalize(glm::cross(Front, WorldUp));
    Up = glm::normalize(glm::cross(Right, Front));
}

glm::mat4 Camera::GetViewMatrix() const {
    return glm::lookAt(Position, Position + Front, Up);
}

glm::mat4 Camera::GetProjectionMatrix() const {
    return glm::perspective(glm::radians(Fov), AspectRatio, 0.1f, 100.0f);
}
