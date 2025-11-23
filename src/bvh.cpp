#include "bvh.h"
#include "utils/model.h" // for Model/mesh layout
#include <algorithm>
#include <vector>

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

    glm::vec3 bmin(1e30f), bmax(-1e30f);
    for (int i = begin; i < end; ++i) {
        const auto &T = tris[refs[i].triIndex];
        bmin = glm::min(bmin, tri_min(T));
        bmax = glm::max(bmax, tri_max(T));
    }

    const int count = end - begin;
    const int myIndex = static_cast<int>(nodes.size());
    nodes.push_back({});
    nodes[myIndex].bmin = bmin;
    nodes[myIndex].bmax = bmax;

    if (count <= leafMax) {
        nodes[myIndex].left = -1;
        nodes[myIndex].right = -1;
        nodes[myIndex].first = begin;
        nodes[myIndex].count = count;
        return myIndex;
    }

    // choose split axis by box extent
    glm::vec3 e = bmax - bmin;
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
void upload_bvh_tbos(const std::vector<BVHNode> &nodes, const std::vector<CPU_Triangle> &tris, GLuint &outNodeTex,
                     GLuint &outNodeBuf, GLuint &outTriTex, GLuint &outTriBuf) {
    // Pack nodes: 3 texels per node (RGBA32F each)
    //  tex0 = [bmin.x, bmin.y, bmin.z, left]
    //  tex1 = [bmax.x, bmax.y, bmax.z, right]
    //  tex2 = [first,  count,  0,       0]
    std::vector<float> nodeData;
    nodeData.reserve(nodes.size() * 12);
    for (const auto &n: nodes) {
        nodeData.push_back(n.bmin.x);
        nodeData.push_back(n.bmin.y);
        nodeData.push_back(n.bmin.z);
        nodeData.push_back(static_cast<float>(n.left));
        nodeData.push_back(n.bmax.x);
        nodeData.push_back(n.bmax.y);
        nodeData.push_back(n.bmax.z);
        nodeData.push_back(static_cast<float>(n.right));
        nodeData.push_back(static_cast<float>(n.first));
        nodeData.push_back(static_cast<float>(n.count));
        nodeData.push_back(0.0f);
        nodeData.push_back(0.0f);
    }

    if (!outNodeBuf)
        glGenBuffers(1, &outNodeBuf);
    glBindBuffer(GL_TEXTURE_BUFFER, outNodeBuf);
    glBufferData(GL_TEXTURE_BUFFER, nodeData.size() * sizeof(float), nodeData.data(), GL_STATIC_DRAW);
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
    glBufferData(GL_TEXTURE_BUFFER, triData.size() * sizeof(float), triData.data(), GL_STATIC_DRAW);
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
