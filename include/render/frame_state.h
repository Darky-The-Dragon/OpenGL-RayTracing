#pragma once
#include <glm/glm.hpp>

namespace rt {
    /**
     * @struct FrameState
     * @brief Stores camera matrices and temporal information needed per frame.
     *
     * The renderer uses this struct to compute motion vectors, accumulate
     * temporal history, and determine whether reprojection is valid between
     * frames. Both current and previous view-projection matrices are tracked,
     * along with camera positions and the current pixel jitter value used
     * for TAA-style sampling.
     */
    struct FrameState {
        // ---------------------------------------------------------------------
        // Matrices (current / previous)
        // ---------------------------------------------------------------------

        /// Current frame's view matrix.
        glm::mat4 currView{1};

        /// Current frame's projection matrix.
        glm::mat4 currProj{1};

        /// Current frame's combined projection * view matrix.
        glm::mat4 currViewProj{1};

        /// Previous frame's view-projection matrix (used for motion reprojection).
        glm::mat4 prevViewProj{1};

        // ---------------------------------------------------------------------
        // Camera positions
        // ---------------------------------------------------------------------

        /// Current frame camera position in world space.
        glm::vec3 currCamPos{0.0f};

        /// Previous frame camera position.
        glm::vec3 prevCamPos{0.0f};

        // ---------------------------------------------------------------------
        // Jitter for TAA / accumulation
        // ---------------------------------------------------------------------

        /**
         * @brief Per-frame subpixel jitter offset.
         *
         * Jitter is expressed in pixel units in the range [-0.5, 0.5].
         * This offset is added to the projection matrix to enable
         * stochastic sampling patterns in the accumulation renderer.
         */
        glm::vec2 jitter{0.0f};

        // ---------------------------------------------------------------------
        // Frame lifecycle
        // ---------------------------------------------------------------------

        /**
         * @brief Records the matrices and camera position for the current frame.
         *
         * Called at the start of each frame, before rendering begins.
         *
         * @param V      Current view matrix.
         * @param P      Current projection matrix.
         * @param camPos Current camera position in world space.
         */
        void beginFrame(const glm::mat4 &V, const glm::mat4 &P, const glm::vec3 &camPos) {
            currView = V;
            currProj = P;
            currViewProj = P * V;
            currCamPos = camPos;
        }

        /**
         * @brief Stores the current matrices into the previous-frame slots.
         *
         * Called at the end of each frame after presentation. This data
         * becomes the basis for motion vector computation in the next frame.
         */
        void endFrame() {
            prevViewProj = currViewProj;
            prevCamPos = currCamPos;
        }
    };
} // namespace rt
