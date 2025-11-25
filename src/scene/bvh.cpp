#include <glm/gtc/matrix_transform.hpp>
#include "scene/model.h"
#include "scene/bvh.h"
#include <algorithm>
#include <vector>
#include <memory>

// -------- AABB helpers -----------
static glm::vec3 tri_min(const CPU_Triangle &t) {
    const glm::vec3 v1 = t.v0 + t.e1;
    const glm::vec3 v2 = t.v0 + t.e2;
    return glm::min(t.v0, glm::min(v1, v2));
}

static glm::vec3 tri_max(const CPU_Triangle &t) {
    const glm::vec3 v1 = t.v0 + t.e1;
    const glm::vec3 v2 = t.v0 + t.e2;
    return glm::max(t.v0, glm::max(v1, v2));
}

static glm::vec3 tri_centroid(const CPU_Triangle &t) {
    const glm::vec3 v1 = t.v0 + t.e1;
    const glm::vec3 v2 = t.v0 + t.e2;
    return (t.v0 + v1 + v2) * (1.0f / 3.0f);
}

// -------- BVH builder (median split) -----------
struct BuildRef {
    int triIndex;
    glm::vec3 c; // centroid
};

static int build_recursive(std::vector<BVHNode> &nodes, const std::vector<CPU_Triangle> &tris,
                           std::vector<BuildRef> &refs, const int begin, const int end, const int leafMax = 8) {
    glm::vec3 bMin(1e30f), bMax(-1e30f);
    for (int i = begin; i < end; ++i) {
        const auto &T = tris[refs[i].triIndex];
        bMin = glm::min(bMin, tri_min(T));
        bMax = glm::max(bMax, tri_max(T));
    }

    const int count = end - begin;
    const int myIndex = static_cast<int>(nodes.size());
    nodes.push_back({});
    nodes[myIndex].bMin = bMin;
    nodes[myIndex].bMax = bMax;

    if (count <= leafMax) {
        nodes[myIndex].left = -1;
        nodes[myIndex].right = -1;
        nodes[myIndex].first = begin;
        nodes[myIndex].count = count;
        return myIndex;
    }

    // choose split axis by box extent
    const glm::vec3 e = bMax - bMin;
    int axis = (e.x > e.y) ? ((e.x > e.z) ? 0 : 2) : ((e.y > e.z) ? 1 : 2);

    const int mid = (begin + end) / 2;
    std::nth_element(refs.begin() + begin, refs.begin() + mid, refs.begin() + end,
                     [axis](const BuildRef &a, const BuildRef &b) { return a.c[axis] < b.c[axis]; });

    const int leftIdx = build_recursive(nodes, tris, refs, begin, mid, leafMax);
    const int rightIdx = build_recursive(nodes, tris, refs, mid, end, leafMax);

    nodes[myIndex].left = leftIdx;
    nodes[myIndex].right = rightIdx;
    nodes[myIndex].first = -1;
    nodes[myIndex].count = 0;
    return myIndex;
}

std::vector<BVHNode> build_bvh(std::vector<CPU_Triangle> &tris) {
    std::vector<BVHNode> nodes;
    if (tris.empty()) return nodes;

    std::vector<BuildRef> refs(tris.size());
    for (size_t i = 0; i < tris.size(); ++i) {
        refs[i].triIndex = static_cast<int>(i);
        refs[i].c = tri_centroid(tris[i]);
    }

    nodes.reserve(tris.size() * 2);
    build_recursive(nodes, tris, refs, 0, static_cast<int>(refs.size()), 8);

    // Reorder triangles to match leaf ranges for better locality.
    std::vector<CPU_Triangle> remapped;
    remapped.reserve(tris.size());

    // Simple DFS stack:
    std::vector<int> stack;
    stack.push_back(0);

    while (!stack.empty()) {
        const int n = stack.back();
        stack.pop_back();
        const auto &node = nodes[n];

        if (node.isLeaf()) {
            for (int i = 0; i < node.count; ++i) {
                remapped.push_back(tris[refs[node.first + i].triIndex]);
            }
            const int base = static_cast<int>(remapped.size()) - node.count;
            // store back remapped base
            nodes[n].first = base;
        } else {
            stack.push_back(node.left);
            stack.push_back(node.right);
        }
    }

    tris = std::move(remapped);
    return nodes;
}

// -------- Upload to TBOs (GL_TEXTURE_BUFFER) -----------
void upload_bvh_tbo(const std::vector<BVHNode> &nodes, const std::vector<CPU_Triangle> &tris, GLuint &outNodeTex,
                    GLuint &outNodeBuf, GLuint &outTriTex, GLuint &outTriBuf) {
    // Pack nodes: 3 texels per node (RGBA32F each)
    //  tex0 = [bMin.x, bMin.y, bMin.z, left]
    //  tex1 = [bMax.x, bMax.y, bMax.z, right]
    //  tex2 = [first,  count,  0,       0]
    std::vector<float> nodeData;
    nodeData.reserve(nodes.size() * 12);
    for (const auto &n: nodes) {
        nodeData.push_back(n.bMin.x);
        nodeData.push_back(n.bMin.y);
        nodeData.push_back(n.bMin.z);
        nodeData.push_back(static_cast<float>(n.left));
        nodeData.push_back(n.bMax.x);
        nodeData.push_back(n.bMax.y);
        nodeData.push_back(n.bMax.z);
        nodeData.push_back(static_cast<float>(n.right));
        nodeData.push_back(static_cast<float>(n.first));
        nodeData.push_back(static_cast<float>(n.count));
        nodeData.push_back(0.0f);
        nodeData.push_back(0.0f);
    }

    if (!outNodeBuf)
        glGenBuffers(1, &outNodeBuf);
    glBindBuffer(GL_TEXTURE_BUFFER, outNodeBuf);
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(nodeData.size() * sizeof(float)), nodeData.data(),
                 GL_STATIC_DRAW);
    if (!outNodeTex)
        glGenTextures(1, &outNodeTex);
    glBindTexture(GL_TEXTURE_BUFFER, outNodeTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, outNodeBuf);

    // Pack triangles: 3 texels per tri
    //  tex0 = [v0.x, v0.y, v0.z, 0]
    //  tex1 = [e1.x, e1.y, e1.z, 0]
    //  tex2 = [e2.x, e2.y, e2.z, 0]
    std::vector<float> triData;
    triData.reserve(tris.size() * 12);
    for (const auto &t: tris) {
        triData.push_back(t.v0.x);
        triData.push_back(t.v0.y);
        triData.push_back(t.v0.z);
        triData.push_back(0.0f);
        triData.push_back(t.e1.x);
        triData.push_back(t.e1.y);
        triData.push_back(t.e1.z);
        triData.push_back(0.0f);
        triData.push_back(t.e2.x);
        triData.push_back(t.e2.y);
        triData.push_back(t.e2.z);
        triData.push_back(0.0f);
    }

    if (!outTriBuf)
        glGenBuffers(1, &outTriBuf);
    glBindBuffer(GL_TEXTURE_BUFFER, outTriBuf);
    glBufferData(
        GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(triData.size() * sizeof(float)), triData.data(),GL_STATIC_DRAW);
    if (!outTriTex)
        glGenTextures(1, &outTriTex);
    glBindTexture(GL_TEXTURE_BUFFER, outTriTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, outTriBuf);

    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
}

// -------- Extract triangles from Model -----------
void gather_model_triangles(const Model &model, const glm::mat4 &M, std::vector<CPU_Triangle> &outTris) {
    // Assumes LearnOpenGL-like:
    // mesh.vertices[i].Position (glm::vec3)
    // mesh.indices (uint32_t triplets)
    for (const auto &mesh: model.meshes) {
        const auto &V = mesh.vertices;
        const auto &I = mesh.indices;
        for (size_t k = 0; k + 2 < I.size(); k += 3) {
            auto p0 = glm::vec3(M * glm::vec4(V[I[k]].Position, 1.0f));
            auto p1 = glm::vec3(M * glm::vec4(V[I[k + 1]].Position, 1.0f));
            auto p2 = glm::vec3(M * glm::vec4(V[I[k + 2]].Position, 1.0f));
            CPU_Triangle T{};
            T.v0 = p0;
            T.e1 = p1 - p0;
            T.e2 = p2 - p0;
            outTris.push_back(T);
        }
    }
}

bool rebuild_bvh_from_model_path(const char *path, const glm::mat4 &modelTransform, std::unique_ptr<Model> &bvhModel,
                                 int &outNodeCount, int &outTriCount, BVHHandle &handle) {
    handle.release();

    // --- Reload model ---
    bvhModel = std::make_unique<Model>(path);
    if (!bvhModel || bvhModel->meshes.empty()) {
        bvhModel.reset();
        outNodeCount = 0;
        outTriCount = 0;
        return false;
    }

    // --- Extract triangles with the provided model transform ---
    std::vector<CPU_Triangle> triCPU;
    gather_model_triangles(*bvhModel, modelTransform, triCPU);

    // --- Build BVH on CPU ---
    const std::vector<BVHNode> nodesCPU = build_bvh(triCPU);
    outNodeCount = static_cast<int>(nodesCPU.size());
    outTriCount = static_cast<int>(triCPU.size());

    // --- Upload to GPU as TBOs ---
    upload_bvh_tbo(nodesCPU, triCPU, handle.nodeTex, handle.nodeBuf, handle.triTex, handle.triBuf);

    return true;
}
