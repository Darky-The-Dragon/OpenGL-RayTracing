#include "rt/accum.h"
#include <iostream>

namespace rt {
    GLuint Accum::createAccumTex(int w, int h) {
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return t;
    }

    void Accum::release() {
        if (tex[0]) {
            glDeleteTextures(1, &tex[0]);
            tex[0] = 0;
        }
        if (tex[1]) {
            glDeleteTextures(1, &tex[1]);
            tex[1] = 0;
        }
        if (fbo) {
            glDeleteFramebuffers(1, &fbo);
            fbo = 0;
        }
        width = height = 0;
        writeIdx = 0;
        frameIndex = 0;
    }

    Accum::~Accum() {
        release();
    }

    Accum::Accum(Accum &&o) noexcept {
        *this = std::move(o);
    }

    Accum &Accum::operator=(Accum &&o) noexcept {
        if (this == &o) return *this;
        release();
        fbo = o.fbo;
        o.fbo = 0;
        tex[0] = o.tex[0];
        o.tex[0] = 0;
        tex[1] = o.tex[1];
        o.tex[1] = 0;
        writeIdx = o.writeIdx;
        o.writeIdx = 0;
        frameIndex = o.frameIndex;
        o.frameIndex = 0;
        width = o.width;
        height = o.height;
        o.width = o.height = 0;
        return *this;
    }

    void Accum::recreate(int w, int h) {
        if (w <= 0 || h <= 0) return;

        // If size unchanged, keep existing textures
        if (w == width && h == height && tex[0] && tex[1] && fbo) {
            reset();
            return;
        }

        if (!fbo)
            glGenFramebuffers(1, &fbo);

        if (tex[0])
            glDeleteTextures(1, &tex[0]);
        if (tex[1])
            glDeleteTextures(1, &tex[1]);

        tex[0] = createAccumTex(w, h);
        tex[1] = createAccumTex(w, h);

        width = w;
        height = h;
        writeIdx = 0;
        frameIndex = 0;
    }

    void Accum::bindWriteFBO() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, writeTex(), 0);
        static const GLenum bufs[1] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, bufs);

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "FBO incomplete: 0x" << std::hex << status << std::dec << "\n";
        }
    }

    void Accum::bindWriteFBO_MRT(GLuint posTex, GLuint nrmTex) const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, writeTex(), 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, posTex,     0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, nrmTex,     0);

        static const GLenum bufs[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
        glDrawBuffers(3, bufs);

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "FBO incomplete (MRT): 0x" << std::hex << status << std::dec << "\n";
        }
    }

    void Accum::clear() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, writeTex(), 0);
        static const GLenum bufs[1] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, bufs);
        // Clear to black (linear)
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
} // namespace rt
