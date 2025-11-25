#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glad/gl.h>

struct CPU_Triangle {
    glm::vec3 v0; // position
    glm::vec3 e1; // edge1 = v1 - v0
    glm::vec3 e2; // edge2 = v2 - v0
};

struct BVHNode {
    glm::vec3 bMin;
    glm::vec3 bMax;
    int left; // index of left child or -1
    int right; // index of right child or -1
    int first; // start triangle index in leaf
    int count; // triangle count in leaf (0 if inner)

    [[nodiscard]] bool isLeaf() const {
        return count > 0;
    }
};

/// Build a simple median-split BVH
std::vector<BVHNode> build_bvh(std::vector<CPU_Triangle> &tris);

/// Upload linearized nodes & triangles to GPU as texture buffers (TBOs).
/// Produces two textures + two buffers (so we can delete buffers safely at shutdown).
void upload_bvh_tbo(const std::vector<BVHNode> &nodes, const std::vector<CPU_Triangle> &tris, GLuint &outNodeTex,
                     GLuint &outNodeBuf, GLuint &outTriTex, GLuint &outTriBuf);

/// Utility: extract triangles from a LearnOpenGL-style Model (positions + indices).
/// Applies model matrix (scale/rotate/translate) to positions.
struct Model; // forward
void gather_model_triangles(const Model &model, const glm::mat4 &M, std::vector<CPU_Triangle> &outTris);
