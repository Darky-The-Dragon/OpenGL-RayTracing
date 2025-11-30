#pragma once
#include <string>
#include "glad/gl.h"

/**
 * @brief Creates a placeholder cube map texture.
 *
 * This function generates a minimal valid cube map using solid colors.
 * It is primarily used during initialization when the user has not yet
 * selected an environment map, ensuring that shaders relying on a cube map
 * can still bind a valid texture.
 *
 * @return OpenGL texture handle for the dummy cube map.
 */
GLuint createDummyCubeMap();

/**
 * @brief Loads a cube map from a cross-layout image file.
 *
 * The function expects a single image containing all six cube map faces
 * arranged in a cross pattern (typical for HDR environment maps). The loader
 * slices the source image into individual faces and uploads them into an
 * OpenGL cube map texture.
 *
 * @param path Filesystem path to the cross-layout image.
 * @return OpenGL texture handle for the uploaded cube map. Returns 0 if loading fails.
 */
GLuint loadCubeMapFromCross(const std::string &path);
