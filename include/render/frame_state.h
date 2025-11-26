#pragma once
#include <glm/glm.hpp>

namespace rt {
    struct FrameState {
        // View / projection
        glm::mat4 currView{1};
        glm::mat4 currProj{1};
        glm::mat4 currViewProj{1};

        glm::mat4 prevViewProj{1};

        // Camera positions
        glm::vec3 currCamPos{0.0f};
        glm::vec3 prevCamPos{0.0f};

        // Per–frame pixel jitter (in pixel units, [-0.5, 0.5])
        glm::vec2 jitter{0.0f};

        void beginFrame(const glm::mat4 &V, const glm::mat4 &P, const glm::vec3 &camPos) {
            currView = V;
            currProj = P;
            currViewProj = P * V;
            currCamPos = camPos;
        }

        void endFrame() {
            prevViewProj = currViewProj;
            prevCamPos = currCamPos;
        }
    };
} // namespace rt