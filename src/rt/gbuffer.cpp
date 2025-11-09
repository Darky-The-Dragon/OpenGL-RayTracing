#include "rt/gbuffer.h"
#include <iostream>

namespace rt {

    GLuint GBuffer::makeTex2D(int w, int h, GLenum internalFmt) {
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return t;
    }

    void GBuffer::release() {
        if (posTex) { glDeleteTextures(1, &posTex); posTex = 0; }
        if (nrmTex) { glDeleteTextures(1, &nrmTex); nrmTex = 0; }
        width = height = 0;
    }

    void GBuffer::recreate(int w, int h) {
        if (w <= 0 || h <= 0) return;
        if (w == width && h == height && posTex && nrmTex) return;

        release();
        // Use RGBA16F backing to keep upload format simple (GL_RGBA/GL_FLOAT).
        posTex = makeTex2D(w, h, GL_RGBA16F);
        nrmTex = makeTex2D(w, h, GL_RGBA16F);

        width = w; height = h;
    }

} // namespace rt