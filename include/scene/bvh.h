#pragma once
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <glad/gl.h>

class Model; // forward decl to avoid include-order brittleness

struct CPU_Triangle {
    glm::vec3 v0; // position
    glm::vec3 e1; // edge1 = v1 - v0
    glm::vec3 e2; // edge2 = v2 - v0
};

struct BVHHandle {
    GLuint nodeTex = 0;
    GLuint nodeBuf = 0;
    GLuint triTex = 0;
    GLuint triBuf = 0;

    void release() {
        if (nodeTex) { glDeleteTextures(1, &nodeTex); nodeTex = 0; }
        if (triTex)  { glDeleteTextures(1, &triTex);  triTex = 0; }
        if (nodeBuf) { glDeleteBuffers(1, &nodeBuf); nodeBuf = 0; }
        if (triBuf)  { glDeleteBuffers(1, &triBuf);  triBuf = 0; }
    }
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
void gather_model_triangles(const Model &model, const glm::mat4 &M, std::vector<CPU_Triangle> &outTris);

// High-level helper: load a model, build a BVH for it, and upload to GPU TBOs.
// - Deletes/replaces any previous Model* and GL objects passed in.
// - Returns true on success, false if the model could not be loaded.
//
bool rebuild_bvh_from_model_path(const char *path, const glm::mat4 &modelTransform, std::unique_ptr<Model> &bvhModel,
                                 int &outNodeCount, int &outTriCount, BVHHandle &handle);
