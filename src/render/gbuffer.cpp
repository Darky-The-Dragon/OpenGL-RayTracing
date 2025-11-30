#include "render/gbuffer.h"

namespace rt {
    // Create a 2D float texture with nearest filtering.
    // Used for position and normal buffers.
    GLuint GBuffer::makeTex2D(const int w, const int h, const GLenum internalFmt) {
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);

        // Using GL_RGBA/GL_FLOAT for simplicity in shaders and uploads.
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFmt),
                     w, h, 0,
                     GL_RGBA, GL_FLOAT, nullptr);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return t;
    }

    // Destroy both GBuffer textures.
    // Called on resize or on shutdown.
    void GBuffer::release() {
        if (posTex) {
            glDeleteTextures(1, &posTex);
            posTex = 0;
        }
        if (nrmTex) {
            glDeleteTextures(1, &nrmTex);
            nrmTex = 0;
        }
        width = 0;
        height = 0;
    }

    // Reallocate the G-buffer textures if size changed.
    // If same size, do nothing.
    void GBuffer::recreate(const int w, const int h) {
        if (w <= 0 || h <= 0)
            return;

        // Early-out if size matches and textures are valid
        if (w == width && h == height && posTex && nrmTex)
            return;

        // Allocate new textures
        release();

        // Use RGBA16F for both position and normal to keep things consistent.
        posTex = makeTex2D(w, h, GL_RGBA16F);
        nrmTex = makeTex2D(w, h, GL_RGBA16F);

        width = w;
        height = h;
    }
} // namespace rt
