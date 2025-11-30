#pragma once

#include "app/state.h"

/**
 * @brief Executes the ray/path tracing rendering path.
 *
 * This function drives the full ray tracing pipeline for the current frame.
 * It binds the appropriate accumulation FBO, updates uniforms, dispatches
 * the main ray tracing shader, and handles accumulation reset when needed.
 *
 * @param app          Global application state (accum buffers, shaders, camera, params).
 * @param fbw          Framebuffer width.
 * @param fbh          Framebuffer height.
 * @param cameraMoved  True if the camera moved this frame, forcing accumulation reset.
 * @param currView     Current view matrix.
 * @param currProj     Current projection matrix.
 */
void renderRay(AppState &app, int fbw, int fbh, bool cameraMoved, const glm::mat4 &currView, const glm::mat4 &currProj);

/**
 * @brief Executes the rasterization rendering path for debugging or comparison.
 *
 * This function renders the scene using the standard raster pipeline. It is
 * primarily used as a reference image for comparison with the ray tracing
 * output, and for rendering BVH debug geometry when enabled.
 *
 * @param app       Read-only application state containing models and shaders.
 * @param fbw       Framebuffer width.
 * @param fbh       Framebuffer height.
 * @param currView  Current view matrix.
 * @param currProj  Current projection matrix.
 */
void renderRaster(const AppState &app, int fbw, int fbh, const glm::mat4 &currView, const glm::mat4 &currProj);
