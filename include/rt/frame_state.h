#pragma once
#include <glm/glm.hpp>

namespace rt {

    struct FrameState {
        glm::mat4 currView{1}, currProj{1}, currViewProj{1};
        glm::mat4 prevViewProj{1};
        glm::vec3 currCamPos{0}, prevCamPos{0};
        glm::vec2 jitter{0}; // reserved for TAA jitter later

        void beginFrame(const glm::mat4& V, const glm::mat4& P, const glm::vec3& camPos) {
            currView     = V;
            currProj     = P;
            currViewProj = P * V;
            currCamPos   = camPos;
        }
        void endFrame() {
            prevViewProj = currViewProj;
            prevCamPos   = currCamPos;
        }
    };

} // namespace rt