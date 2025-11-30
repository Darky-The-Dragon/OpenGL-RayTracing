#pragma once
#include <glad/gl.h>

/// Rendering utilities and resources for the ray tracing pipeline.
namespace rt {
    /**
     * @class Accum
     * @brief Manages the temporal accumulation buffer used by the path tracer.
     *
     * This class encapsulates all GPU resources required for progressive
     * accumulation (ping-pong textures), motion-vector storage, and MRT output.
     * It handles creation, resizing, clearing, and binding of the underlying
     * framebuffer object.
     *
     * The accumulation buffer stores:
     *  - linear HDR color (RGBA16F)
     *  - screen-space motion vectors (RG16F)
     *
     * The ping-pong scheme alternates between two color textures each frame.
     * One texture is written to (current frame), while the other is sampled
     * from (previous accumulated result). The class keeps track of both the
     * active index and the current accumulation frame count.
     */
    class Accum {
    public:
        /// Accumulation FBO handle.
        GLuint fbo = 0;

        /// Ping-pong accumulation textures (RGBA16F).
        GLuint tex[2] = {0, 0};

        /// Motion vector texture (RG16F), storing NDC delta per pixel.
        GLuint motionTex = 0;

        /// Index of the accumulation texture being written to this frame.
        int writeIdx = 0;

        /// Number of frames accumulated so far.
        int frameIndex = 0;

        /// Current dimensions of the accumulation buffers.
        int width = 0, height = 0;

        /// Default constructor (creates an empty, uninitialized accumulator).
        Accum() = default;

        /// Destructor does not auto-release; release() must be called explicitly.
        ~Accum() = default;

        /// Non-copyable to avoid double-free of GL objects.
        Accum(const Accum &) = delete;

        Accum &operator=(const Accum &) = delete;

        /// Move constructor — transfers ownership of GL resources.
        Accum(Accum &&other) noexcept;

        /// Move assignment — transfers ownership and invalidates the source.
        Accum &operator=(Accum &&other) noexcept;

        /**
         * @brief Resets the accumulated frame count.
         *
         * This should be called when camera movement or parameter changes
         * invalidate the temporal history (e.g., exposure change, SPP change).
         * The textures themselves are not recreated; only the counters reset.
         */
        void reset();

        /**
         * @brief Creates or recreates all accumulation textures.
         *
         * Called on window resize or initial startup. This function allocates:
         *  - two RGBA16F accumulation textures
         *  - one RG16F motion vector texture
         * and attaches them to the FBO. Previous resources are deleted.
         *
         * @param w New width of render targets.
         * @param h New height of render targets.
         */
        void recreate(int w, int h);

        /**
         * @brief Binds the accumulation FBO with only COLOR0 active.
         *
         * COLOR0 is set to the current write texture.
         * Useful for simple accumulation without motion vectors.
         */
        void bindWriteFBO() const;

        /**
         * @brief Binds the FBO to write both accumulation color and motion vectors.
         *
         * - COLOR0 → accumulation (RGBA16F)
         * - COLOR1 → motion vectors (RG16F)
         */
        void bindWriteFBO_ColorAndMotion() const;

        /**
         * @brief Binds FBO with 4 MRT targets for combined RT + GBuffer output.
         *
         * - COLOR0 → accumulation write (RGBA16F)
         * - COLOR1 → motion (RG16F)
         * - COLOR2 → world-space position (posTex)
         * - COLOR3 → world-space normal (nrmTex)
         *
         * @param posTex World-space position buffer texture.
         * @param nrmTex World-space normal buffer texture.
         */
        void bindWriteFBO_MRT(GLuint posTex, GLuint nrmTex) const;

        /**
         * @brief Clears the active write buffers to zero.
         *
         * This clears both COLOR0 and COLOR1. Useful when switching modes
         * or after resetting accumulation.
         */
        void clear() const;

        /**
         * @brief Advances the accumulation frame and flips the ping-pong index.
         *
         * Should be called once per rendered frame after the present pass.
         */
        void swapAfterFrame() {
            frameIndex++;
            writeIdx = 1 - writeIdx;
        }

        /**
         * @return The texture containing the previous frame's accumulated result.
         */
        [[nodiscard]] GLuint readTex() const { return tex[1 - writeIdx]; }

        /**
         * @return The texture being written into this frame.
         */
        [[nodiscard]] GLuint writeTex() const { return tex[writeIdx]; }

        /**
         * @brief Releases all GPU-side resources owned by the accumulator.
         *
         * Deletes FBO, ping-pong textures, and motion texture.
         * After calling this, the object returns to an uninitialized state.
         */
        void release();

    private:
        /**
         * @brief Creates an RGBA16F accumulation texture of size w × h.
         *
         * @return The OpenGL texture handle.
         */
        static GLuint createAccumTex(int w, int h);

        /**
         * @brief Creates an RG16F motion-vector texture of size w × h.
         *
         * @return The OpenGL texture handle.
         */
        static GLuint createRG16F(int w, int h);
    };
} // namespace rt
