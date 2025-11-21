#pragma once
#include <glad/gl.h>

// Minimal manager for temporal accumulation ping-pong + motion vectors.
namespace rt {
    class Accum {
    public:
        GLuint fbo = 0;
        GLuint tex[2] = {0, 0}; // ping-pong accumulation (linear color)
        GLuint motionTex = 0; // RG16F motion (NDC delta)
        int writeIdx = 0; // which tex we write *this* frame
        int frameIndex = 0; // number of completed frames in the accumulation
        int width = 0, height = 0;

        Accum() = default;

        ~Accum();

        // non-copyable (prevents double-free of GL objects)
        Accum(const Accum &) = delete;

        Accum &operator=(const Accum &) = delete;

        // movable
        Accum(Accum &&other) noexcept;

        Accum &operator=(Accum &&other) noexcept;

        // Reset accumulation (e.g., camera moved, params changed).
        void reset();

        // Create/Recreate accumulation targets (also resets counters).
        void recreate(int w, int h);

        // Bind FBO to write the current frame (COLOR0 = writeTex()).
        void bindWriteFBO() const;

        // Bind FBO to write COLOR0 (accum write) + COLOR1 (motion RG16F).
        void bindWriteFBO_ColorAndMotion() const;

        // Bind FBO with multiple render targets:
        //  - COLOR0 = accumulation write
        //  - COLOR1 = motion (RG16F)
        //  - COLOR2 = gbuffer position (WS)
        //  - COLOR3 = gbuffer normal (WS)
        void bindWriteFBO_MRT(GLuint posTex, GLuint nrmTex) const;

        // Clear current write target(s) (COLOR0 and COLOR1) to zero.
        void clear() const;

        // After presenting, advance frame and swap ping-pong.
        void swapAfterFrame() {
            frameIndex++;
            writeIdx = 1 - writeIdx;
        }

        // Helpers
        GLuint readTex() const { return tex[1 - writeIdx]; }
        GLuint writeTex() const { return tex[writeIdx]; }

    private:
        static GLuint createAccumTex(int w, int h); // RGBA16F
        static GLuint createRG16F(int w, int h); // RG16F (motion)
        void release();
    };
} // namespace rt
