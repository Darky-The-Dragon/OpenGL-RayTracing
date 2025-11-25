#pragma once
#include <glad/gl.h>

namespace rt {

    // World-space position + normal, sized to the framebuffer.
    class GBuffer {
    public:
        GLuint posTex = 0;  // RGB16F (xyz world pos), A unused
        GLuint nrmTex = 0;  // RGB16F (xyz world normal), A unused
        int    width = 0, height = 0;

        GBuffer() = default;
        ~GBuffer() { release(); }

        void recreate(int w, int h);
        void release();

    private:
        // Take GLenum here – it matches GL constants like GL_RGBA16F.
        static GLuint makeTex2D(int w, int h, GLenum internalFmt);
    };

} // namespace rt