#include "render/RenderParams.h"
#include <glad/gl.h>
#include <cstring>

void RenderParamsUBO::create() {
    if (!ubo) glGenBuffers(1, &ubo);
}

void RenderParamsUBO::destroy() {
    if (ubo) {
        glDeleteBuffers(1, &ubo);
        ubo = 0;
    }
}

void RenderParamsUBO::upload(const RenderParams &p) {
    if (!ubo) create();
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(RenderParams), &p, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

