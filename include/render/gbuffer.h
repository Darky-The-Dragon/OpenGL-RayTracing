#pragma once
#include <glad/gl.h>

namespace rt {
    /**
     * @class GBuffer
     * @brief Minimal geometry buffer storing world-space position and normal.
     *
     * The GBuffer is sized to match the framebuffer resolution and provides
     * per-pixel world-space attributes required for temporal reprojection,
     * denoising, and certain debug visualizations. It stores:
     *
     *  - posTex : RGB16F world-space position
     *  - nrmTex : RGB16F world-space normal
     *
     * Both textures are allocated as floating-point formats to preserve
     * enough precision for ray tracing and shading calculations.
     */
    class GBuffer {
    public:
        /// RGB16F world-space position (x, y, z). Alpha unused.
        GLuint posTex = 0;

        /// RGB16F world-space normal (x, y, z). Alpha unused.
        GLuint nrmTex = 0;

        /// Dimensions of both GBuffer textures.
        int width = 0, height = 0;

        /// Default constructor (creates an uninitialized GBuffer).
        GBuffer() = default;

        /// Destructor does not auto-release; release() must be called manually.
        ~GBuffer() = default;

        /**
         * @brief Creates or recreates the GBuffer textures.
         *
         * Allocates two floating-point 2D textures:
         *  - posTex as RGB16F (world-space position)
         *  - nrmTex as RGB16F (world-space normal)
         *
         * Called on initial setup or whenever the window is resized.
         *
         * @param w New framebuffer width.
         * @param h New framebuffer height.
         */
        void recreate(int w, int h);

        /**
         * @brief Deletes all GL resources owned by this GBuffer.
         *
         * After calling release(), both posTex and nrmTex are reset to 0.
         */
        void release();

    private:
        /**
         * @brief Creates a 2D floating-point texture of the desired internal format.
         *
         * Used internally by recreate() to construct GBuffer attachments.
         *
         * @param w Width of the texture.
         * @param h Height of the texture.
         * @param internalFmt OpenGL internal format (e.g., GL_RGB16F).
         * @return The OpenGL texture handle.
         */
        static GLuint makeTex2D(int w, int h, GLenum internalFmt);
    };
} // namespace rt
