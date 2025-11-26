#pragma once

#include "app/state.h"

// Ray/path rendering path
void renderRay(AppState &app, int fbw, int fbh, bool cameraMoved, const glm::mat4 &currView, const glm::mat4 &currProj);

// Simple raster path
void renderRaster(AppState &app, int fbw, int fbh, const glm::mat4 &currView, const glm::mat4 &currProj);