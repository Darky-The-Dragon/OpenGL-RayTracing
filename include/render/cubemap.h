#pragma once
#include <string>
#include "glad/gl.h"

GLuint createDummyCubeMap();

GLuint loadCubeMapFromCross(const std::string &path);
