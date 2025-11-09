#pragma once
#include <glad/gl.h>

// Minimal manager for temporal accumulation ping-pong.
namespace rt {
    class Accum {
    public:
        GLuint fbo = 0;
        GLuint tex[2] = {0, 0};
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

        // Create/Recreate accumulation targets (also resets counters).
        void recreate(int w, int h);

        // Reset accumulation (e.g., camera moved, params changed).
        inline void reset() { frameIndex = 0; }

        // Bind FBO to write the current frame (color0 = writeTex()).
        void bindWriteFBO() const;

        // Bind FBO with multiple render targets:
        //  - COLOR0 = accumulation write
        //  - COLOR1 = gbuffer position (WS)
        //  - COLOR2 = gbuffer normal (WS)
        void bindWriteFBO_MRT(GLuint posTex, GLuint nrmTex) const;

        // Clear current write target to 0 (useful after recreate or when nuking history).
        void clear() const;

        // After presenting, advance frame and swap ping-pong.
        inline void swapAfterFrame() {
            frameIndex++;
            writeIdx = 1 - writeIdx;
        }

        // Helpers
        inline GLuint readTex() const { return tex[1 - writeIdx]; }
        inline GLuint writeTex() const { return tex[writeIdx]; }

    private:
        static GLuint createAccumTex(int w, int h);

        void release();
    };
} // namespace rt
