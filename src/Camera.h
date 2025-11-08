#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

class Camera {
public:
    Camera(glm::vec3 position, float yaw, float pitch, float fov, float aspectRatio);

    void ProcessKeyboardInput(GLFWwindow *window, float deltaTime);

    void ProcessMouseMovement(float xOffset, float yOffset);

    void UpdateCameraVectors();

    [[nodiscard]] glm::mat4 GetViewMatrix() const;

    [[nodiscard]] glm::mat4 GetProjectionMatrix() const;

    glm::vec3 Position;
    float Yaw;
    float Pitch;
    float Fov;
    float AspectRatio;
    float MovementSpeed;

private:
    glm::vec3 Front{};
    glm::vec3 Up{};
    glm::vec3 Right{};
    glm::vec3 WorldUp;
};
