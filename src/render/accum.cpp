#include "render/accum.h"
#include <iostream>

namespace rt {
    // Create an RGBA16F texture used for accumulation + M2.
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

    // Create an RG16F texture used for storing motion vectors.
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

    // Release all GL resources owned by this Accum.
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

    // Move constructor: steal GL resources from the source.
    Accum::Accum(Accum &&o) noexcept {
        *this = std::move(o);
    }

    // Move assignment: release current resources and take ownership from o.
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

    // Reset accumulation history without reallocating textures.
    // Clears current write target + motion and rewinds frame index.
    void Accum::reset() {
        frameIndex = 0;
        writeIdx = 0;
        clear(); // clears COLOR0 (current write ping) + motion
    }

    // Ensure accumulation buffers exist at the given size.
    // Reallocates textures/FBO if the size changed, otherwise just resets history.
    void Accum::recreate(int w, int h) {
        if (w <= 0 || h <= 0) return;

        // If size unchanged and resources exist → just reset history.
        if (w == width && h == height && fbo && tex[0] && tex[1] && motionTex) {
            reset();
            return;
        }

        // Recreate everything from scratch.
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

        // Bootstrap: clear both ping targets + motion so history starts clean.
        bindWriteFBO_ColorAndMotion(); {
            static constexpr float zero4[4] = {0.f, 0.f, 0.f, 0.f};
            glClearBufferfv(GL_COLOR, 0, zero4); // accum write target
            glClearBufferfv(GL_COLOR, 1, zero4); // motion (RG16F)
        }

        // Clear the other ping as well.
        swapAfterFrame();
        bindWriteFBO_ColorAndMotion(); {
            static constexpr float zero4[4] = {0.f, 0.f, 0.f, 0.f};
            glClearBufferfv(GL_COLOR, 0, zero4);
            glClearBufferfv(GL_COLOR, 1, zero4);
        }

        // Reset indices for first frame after recreate.
        writeIdx = 0;
        frameIndex = 0;
    }

    // Bind FBO for a simple single-color output (no motion / GBuffer).
    void Accum::bindWriteFBO() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, writeTex(), 0);

        static constexpr GLenum bufs[1] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, bufs);

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "FBO incomplete (Color): 0x"
                    << std::hex << status << std::dec << "\n";
        }
    }

    // Bind FBO for writing color + motion (TAA / motion vectors).
    void Accum::bindWriteFBO_ColorAndMotion() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, writeTex(), 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, motionTex, 0);

        static constexpr GLenum bufs[2] = {
            GL_COLOR_ATTACHMENT0,
            GL_COLOR_ATTACHMENT1
        };
        glDrawBuffers(2, bufs);

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "FBO incomplete (Color+Motion): 0x"
                    << std::hex << status << std::dec << "\n";
        }
    }

    // Bind FBO for MRT: color + motion + world-position + world-normal.
    void Accum::bindWriteFBO_MRT(GLuint posTex, GLuint nrmTex) const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, writeTex(), 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, motionTex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, posTex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, nrmTex, 0);

        static constexpr GLenum bufs[4] = {
            GL_COLOR_ATTACHMENT0,
            GL_COLOR_ATTACHMENT1,
            GL_COLOR_ATTACHMENT2,
            GL_COLOR_ATTACHMENT3
        };
        glDrawBuffers(4, bufs);

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "FBO incomplete (MRT Color+Motion+Pos+Nrm): 0x"
                    << std::hex << status << std::dec << "\n";
        }
    }

    // Clear current write ping + motion to zero.
    void Accum::clear() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, writeTex(), 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, motionTex, 0);

        static constexpr GLenum bufs[2] = {
            GL_COLOR_ATTACHMENT0,
            GL_COLOR_ATTACHMENT1
        };
        glDrawBuffers(2, bufs);

        static constexpr float zero4[4] = {0.f, 0.f, 0.f, 0.f};
        glClearBufferfv(GL_COLOR, 0, zero4);
        glClearBufferfv(GL_COLOR, 1, zero4);
    }
} // namespace rt
