#pragma once
#include <vector>
#include <memory>
#include <glad/gl.h>

class Model; // forward decl to avoid include-order brittleness

/**
 * @struct CPU_Triangle
 * @brief Triangle representation used during BVH construction.
 *
 * The triangle is stored in a format optimized for ray intersection:
 *  - v0  is the first vertex position
 *  - e1  = v1 - v0
 *  - e2  = v2 - v0
 *
 * This layout allows computing ray–triangle intersections with only
 * dot products, avoiding extra recomputation of edges during traversal.
 */
struct CPU_Triangle {
    glm::vec3 v0; ///< First vertex position.
    glm::vec3 e1; ///< Edge from v0 to v1.
    glm::vec3 e2; ///< Edge from v0 to v2.
};

/**
 * @struct BVHHandle
 * @brief Holds GPU-side buffers/textures for a BVH.
 *
 * The BVH is uploaded as two texture buffers (TBOs):
 *  - nodeTex : flattened BVH node array
 *  - triTex  : triangle data for leaf nodes
 *
 * The raw buffer objects are also kept so they can be deleted explicitly
 * at shutdown without risking dangling textures.
 */
struct BVHHandle {
    GLuint nodeTex = 0; ///< Texture buffer containing BVH nodes.
    GLuint nodeBuf = 0; ///< Raw GL buffer for node data.
    GLuint triTex = 0; ///< Texture buffer containing triangles.
    GLuint triBuf = 0; ///< Raw GL buffer for triangle data.

    /**
     * @brief Releases all GPU resources related to the BVH.
     *
     * Safe to call even if some objects were never created.
     */
    void release() {
        if (nodeTex) {
            glDeleteTextures(1, &nodeTex);
            nodeTex = 0;
        }
        if (triTex) {
            glDeleteTextures(1, &triTex);
            triTex = 0;
        }
        if (nodeBuf) {
            glDeleteBuffers(1, &nodeBuf);
            nodeBuf = 0;
        }
        if (triBuf) {
            glDeleteBuffers(1, &triBuf);
            triBuf = 0;
        }
    }
};

/**
 * @struct BVHNode
 * @brief Node structure for a median-split BVH.
 *
 * Internal nodes store a bounding box and indices of their children.
 * Leaf nodes store the starting triangle index and the number of triangles.
 *
 * Conventions:
 *  - left/right = child indices or -1 if none
 *  - first/count are valid only for leaf nodes
 */
struct BVHNode {
    glm::vec3 bMin; ///< Minimum corner of bounding box.
    glm::vec3 bMax; ///< Maximum corner of bounding box.
    int left; ///< Index of left child or -1.
    int right; ///< Index of right child or -1.
    int first; ///< Start index of triangles in leaf.
    int count; ///< Number of triangles in leaf (0 for inner nodes).

    /// @return True if this node is a leaf.
    [[nodiscard]] bool isLeaf() const {
        return count > 0;
    }
};

/**
 * @brief Builds a simple median-split BVH from CPU triangles.
 *
 * The resulting BVH uses a binary tree with splitting based on the
 * longest axis of the node bounding box, partitioned by median position.
 *
 * @param tris Input/output triangle list. Order may be modified.
 * @return Linear array of BVHNode, representing the flattened tree.
 */
std::vector<BVHNode> build_bvh(std::vector<CPU_Triangle> &tris);

/**
 * @brief Uploads BVH nodes and triangles to GPU texture buffers (TBOs).
 *
 * Two pairs of objects are created:
 *  - outNodeBuf + outNodeTex
 *  - outTriBuf  + outTriTex
 *
 * This design allows shutting down cleanly by deleting buffers while
 * the TBO textures reference them indirectly.
 *
 * @param nodes     Flattened BVH node array.
 * @param tris      Triangle list associated with the BVH.
 * @param outNodeTex Output: texture buffer containing nodes.
 * @param outNodeBuf Output: buffer for node data.
 * @param outTriTex  Output: texture buffer containing triangles.
 * @param outTriBuf  Output: buffer for triangle data.
 */
void upload_bvh_tbo(const std::vector<BVHNode> &nodes, const std::vector<CPU_Triangle> &tris, GLuint &outNodeTex,
                    GLuint &outNodeBuf, GLuint &outTriTex, GLuint &outTriBuf);

/**
 * @brief Extracts triangles from a Model into CPU triangle format.
 *
 * The function reads vertex/index buffers from a LearnOpenGL-style
 * Model class, converts triangles into CPU_Triangle format, and
 * applies a model transformation matrix to the vertex positions.
 *
 * @param model     Source Model containing positions and indices.
 * @param M         Transform to apply to all triangle vertices.
 * @param outTris   Output vector of CPU_Triangle structures.
 */
void gather_model_triangles(const Model &model, const glm::mat4 &M, std::vector<CPU_Triangle> &outTris);

/**
 * @brief High-level helper for loading a model and building its BVH.
 *
 * Loads a model from disk, extracts triangles, builds a BVH, and
 * uploads the resulting nodes and triangle data into GPU buffers/TBOs.
 *
 * Old BVH data and the previous model (if any) are deleted and replaced.
 *
 * @param path            File path to the model to load.
 * @param modelTransform  Transform applied to the model geometry.
 * @param bvhModel        Output unique_ptr containing the loaded model.
 * @param outNodeCount    Output number of BVH nodes.
 * @param outTriCount     Output number of triangles.
 * @param handle          Output BVHHandle whose textures/buffers will be filled.
 *
 * @return True on success, false if the model failed to load.
 */
bool rebuild_bvh_from_model_path(const char *path, const glm::mat4 &modelTransform, std::unique_ptr<Model> &bvhModel,
                                 int &outNodeCount, int &outTriCount, BVHHandle &handle);
