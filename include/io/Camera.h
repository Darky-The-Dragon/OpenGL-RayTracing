#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

/**
 * @class Camera
 * @brief Basic FPS-style camera used for both raster and ray tracing views.
 *
 * This camera supports WASD movement, mouse-based orientation updates,
 * and maintains a view/projection pair consistent with the user's input.
 * The class exposes only the minimal set of parameters needed by the renderer.
 *
 * Camera orientation follows the typical yaw/pitch convention:
 * - yaw   → rotation around the world Y axis
 * - pitch → rotation around the camera X axis
 *
 * WorldUp is fixed, while Front / Right / Up are derived vectors.
 */
class Camera {
public:
    /**
     * @brief Constructs a camera with explicit position and orientation.
     *
     * @param position     Initial world-space position of the camera.
     * @param yaw          Initial yaw angle (in degrees).
     * @param pitch        Initial pitch angle (in degrees).
     * @param fov          Field of view in degrees.
     * @param aspectRatio  Aspect ratio of the rendering viewport.
     */
    Camera(glm::vec3 position, float yaw, float pitch, float fov, float aspectRatio);

    /**
     * @brief Handles WASD-style keyboard input for camera translation.
     *
     * Movement speed is scaled by deltaTime to ensure consistent movement
     * across different frame rates.
     *
     * @param window     Pointer to the GLFW window for querying key states.
     * @param deltaTime  Time elapsed since the last frame.
     */
    void ProcessKeyboardInput(GLFWwindow *window, float deltaTime);

    /**
     * @brief Updates yaw and pitch based on mouse movement.
     *
     * The offsets represent the raw mouse delta supplied by GLFW.
     * This function clamps pitch to avoid gimbal lock and recalculates
     * the directional vectors afterward.
     *
     * @param xOffset Horizontal mouse movement.
     * @param yOffset Vertical mouse movement.
     */
    void ProcessMouseMovement(float xOffset, float yOffset);

    /**
     * @brief Recomputes Front, Right, and Up vectors from yaw/pitch.
     *
     * This must be called whenever orientation changes. It keeps the
     * camera's orthonormal basis consistent with the user's input.
     */
    void UpdateCameraVectors();

    /**
     * @brief Computes the camera's view matrix.
     *
     * @return A look-at matrix constructed from Position and Front.
     */
    [[nodiscard]] glm::mat4 GetViewMatrix() const;

    /**
     * @brief Computes the perspective projection matrix.
     *
     * @return Perspective matrix based on FOV and aspect ratio.
     */
    [[nodiscard]] glm::mat4 GetProjectionMatrix() const;

    /// World-space position of the camera.
    glm::vec3 Position;

    /// Rotation around the Y axis (in degrees).
    float Yaw;

    /// Rotation around the X axis (in degrees).
    float Pitch;

    /// Vertical field of view (degrees).
    float Fov;

    /// Viewport aspect ratio (width / height).
    float AspectRatio;

    /// Movement speed used for keyboard input.
    float MovementSpeed;

private:
    /// Forward direction derived from yaw/pitch.
    glm::vec3 Front{};

    /// Camera-local up direction.
    glm::vec3 Up{};

    /// Right vector forming the orthonormal basis with Front and Up.
    glm::vec3 Right{};

    /// The world's up direction (constant).
    glm::vec3 WorldUp;
};
