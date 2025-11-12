#include "rt/accum.h"
#include <iostream>

namespace rt {
    // --- helpers -----------------------------------------------------------------
    GLuint Accum::createAccumTex(int w, int h) {
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        return t;
    }

    GLuint Accum::createRG16F(int w, int h) {
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, w, h, 0, GL_RG, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
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
        if (motionTex) {
            glDeleteTextures(1, &motionTex);
            motionTex = 0;
        }
        if (fbo) {
            glDeleteFramebuffers(1, &fbo);
            fbo = 0;
        }
        width = height = 0;
        writeIdx = 0;
        frameIndex = 0;
    }

    // --- lifetime ----------------------------------------------------------------
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
        motionTex = o.motionTex;
        o.motionTex = 0;
        writeIdx = o.writeIdx;
        o.writeIdx = 0;
        frameIndex = o.frameIndex;
        o.frameIndex = 0;
        width = o.width;
        o.width = 0;
        height = o.height;
        o.height = 0;
        return *this;
    }

    // --- API ---------------------------------------------------------------------

    // Start-over without reallocating: zero current write ping + motion and rewind counters.
    void Accum::reset() {
        frameIndex = 0;
        writeIdx = 0;
        clear(); // clears COLOR0 (current write ping) + motion
    }

    void Accum::recreate(int w, int h) {
        if (w <= 0 || h <= 0) return;

        // If size unchanged and resources exist → just reset history
        if (w == width && h == height && fbo && tex[0] && tex[1] && motionTex) {
            reset();
            return;
        }

        // Recreate everything
        if (!fbo)
            glGenFramebuffers(1, &fbo);

        if (tex[0]) {
            glDeleteTextures(1, &tex[0]);
            tex[0] = 0;
        }
        if (tex[1]) {
            glDeleteTextures(1, &tex[1]);
            tex[1] = 0;
        }
        if (motionTex) {
            glDeleteTextures(1, &motionTex);
            motionTex = 0;
        }

        tex[0] = createAccumTex(w, h);
        tex[1] = createAccumTex(w, h);
        motionTex = createRG16F(w, h);

        width = w;
        height = h;

        // --- Bootstrap: clear both ping targets + motion so history starts clean ---
        bindWriteFBO_ColorAndMotion(); {
            const float zero4[4] = {0.f, 0.f, 0.f, 0.f};
            glClearBufferfv(GL_COLOR, 0, zero4); // accum write target
            glClearBufferfv(GL_COLOR, 1, zero4); // motion (RG16F) – extra components ignored
        }
        // Clear the other ping as well
        swapAfterFrame();
        bindWriteFBO_ColorAndMotion(); {
            const float zero4[4] = {0.f, 0.f, 0.f, 0.f};
            glClearBufferfv(GL_COLOR, 0, zero4);
            glClearBufferfv(GL_COLOR, 1, zero4);
        }

        // Reset indices for first frame after recreate
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
            std::cerr << "FBO incomplete (Color): 0x" << std::hex << status << std::dec << "\n";
        }
    }

    void Accum::bindWriteFBO_ColorAndMotion() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, writeTex(), 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, motionTex, 0);

        static const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, bufs);

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "FBO incomplete (Color+Motion): 0x" << std::hex << status << std::dec << "\n";
        }
    }

    void Accum::bindWriteFBO_MRT(GLuint posTex, GLuint nrmTex) const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, writeTex(), 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, posTex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, nrmTex, 0);

        static const GLenum bufs[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
        glDrawBuffers(3, bufs);

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "FBO incomplete (MRT Color+Pos+Nrm): 0x" << std::hex << status << std::dec << "\n";
        }
    }

    void Accum::clear() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        // Attach current write color + motion so we can clear both
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, writeTex(), 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, motionTex, 0);

        static const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, bufs);

        const float zero4[4] = {0.f, 0.f, 0.f, 0.f};
        glClearBufferfv(GL_COLOR, 0, zero4);
        glClearBufferfv(GL_COLOR, 1, zero4);
    }
} // namespace rt
