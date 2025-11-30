// rt_bvh.glsl
#ifndef RT_BVH_GLSL
#define RT_BVH_GLSL

/*
    rt_bvh.glsl – BVH Data Layout and Traversal

    This module implements:
    - GPU-side representations of triangles (TriSOA) and BVH nodes (NodeSOA),
      mirroring the CPU-side layout uploaded via texture buffers (TBOs).
    - Helper functions to fetch triangle and node data from texture buffers.
    - AABB intersection tests (aabbHit) using slab-based ray-box intersection.
    - Triangle intersection (triHit) using the Möller–Trumbore algorithm with
      precomputed (v0, e1, e2) for each triangle.
    - Two traversal routines:
        * traceBVH       – full closest-hit traversal, returning a Hit struct
        * traceBVHShadow – shadow traversal with early-out when occluded

    The BVH is stored as:
    - uBvhTris  : texture buffer containing triangle data (v0, e1, e2)
    - uBvhNodes : texture buffer containing BVH nodes
    and is accessed via integer indices (triIdx, nodeIdx).

    The layout and encodings must match the CPU-side BVH builder.
*/

// -------- BVH fetch helpers ----------

/**
 * @brief Triangle in SOA-style layout (matches CPU_Triangle).
 *
 * v0 is a vertex position, e1 and e2 are edges:
 *   e1 = v1 - v0
 *   e2 = v2 - v0
 *
 * This layout allows efficient Möller–Trumbore ray-triangle intersection.
 */
struct TriSOA {
    vec3 v0;
    vec3 e1;
    vec3 e2;
};

/**
 * @brief Fetches a triangle from the triangle texture buffer.
 *
 * The CPU packs each triangle into 3 texels:
 *   base     : v0.xyz
 *   base + 1 : e1.xyz
 *   base + 2 : e2.xyz
 *
 * @param triIdx Index of the triangle in the flattened array.
 * @return TriSOA containing v0, e1, e2.
 */
TriSOA triFetch(int triIdx) {
    int base = triIdx * 3;
    vec4 t0 = texelFetch(uBvhTris, base + 0);
    vec4 t1 = texelFetch(uBvhTris, base + 1);
    vec4 t2 = texelFetch(uBvhTris, base + 2);
    TriSOA T;
    T.v0 = t0.xyz;
    T.e1 = t1.xyz;
    T.e2 = t2.xyz;
    return T;
}

/**
 * @brief BVH node layout (matches BVHNode on the CPU).
 *
 * Each node is stored as 3 texels:
 *   texel 0: bmin.xyz, left
 *   texel 1: bmax.xyz, right
 *   texel 2: first, count, (z,w unused)
 *
 * Children indices < 0 indicate leaf/internal conventions defined on the CPU.
 */
struct NodeSOA {
    vec3 bmin; int left;
    vec3 bmax; int right;
    int first; int count;
};

/**
 * @brief Fetches a BVH node from the node texture buffer.
 *
 * Integer data is encoded in the .w components and reconstructed here.
 *
 * @param nodeIdx Index of the node in the flattened node array.
 * @return NodeSOA with bounding box and child/leaf info.
 */
NodeSOA nodeFetch(int nodeIdx) {
    int base = nodeIdx * 3;
    vec4 n0 = texelFetch(uBvhNodes, base + 0);
    vec4 n1 = texelFetch(uBvhNodes, base + 1);
    vec4 n2 = texelFetch(uBvhNodes, base + 2);
    NodeSOA N;
    N.bmin = n0.xyz; N.left = int(n0.w + 0.5);
    N.bmax = n1.xyz; N.right = int(n1.w + 0.5);
    N.first = int(n2.x + 0.5);
    N.count = int(n2.y + 0.5);
    return N;
}

// -----------------------------------------------------------------------------
// Ray–AABB intersection
// -----------------------------------------------------------------------------

/**
 * @brief Ray–AABB intersection test using the slab method.
 *
 * The ray is represented by origin ro and reciprocal direction rdInv.
 * Returns true if the ray intersects the box [bmin, bmax] with a positive
 * intersection interval. The intersection range is returned via tminOut /
 * tmaxOut, clamped to t ≥ 0.
 *
 * @param ro       Ray origin.
 * @param rdInv    Reciprocal ray direction (1.0 / rd).
 * @param bmin     AABB minimum corner.
 * @param bmax     AABB maximum corner.
 * @param tminOut  Output minimum hit distance.
 * @param tmaxOut  Output maximum hit distance.
 * @return True if the AABB is hit, false otherwise.
 */
bool aabbHit(vec3 ro, vec3 rdInv, vec3 bmin, vec3 bmax, out float tminOut, out float tmaxOut) {
    vec3 t0 = (bmin - ro) * rdInv;
    vec3 t1 = (bmax - ro) * rdInv;
    vec3 tsm = min(t0, t1);
    vec3 tbg = max(t0, t1);
    float tmin = max(max(tsm.x, tsm.y), max(tsm.z, 0.0));
    float tmax = min(min(tbg.x, tbg.y), tbg.z);
    tminOut = tmin;
    tmaxOut = tmax;
    return tmax >= tmin;
}

// -----------------------------------------------------------------------------
// Ray–Triangle intersection (Möller–Trumbore)
// -----------------------------------------------------------------------------

/**
 * @brief Ray–triangle intersection using Möller–Trumbore.
 *
 * Triangle data is provided via TriSOA with precomputed (v0, e1, e2).
 * Returns true on a valid hit in front of the ray origin and within [uEPS, tMax].
 *
 * @param ro   Ray origin.
 * @param rd   Ray direction (normalized).
 * @param T    Triangle data (v0, e1, e2).
 * @param tMax Maximum distance to consider a valid hit.
 * @param t    Output hit distance.
 * @param n    Output shading normal (unnormalized cross of edges, normalized here).
 * @return True if the triangle is hit, false otherwise.
 */
bool triHit(vec3 ro, vec3 rd, TriSOA T, float tMax, out float t, out vec3 n) {
    vec3 pvec = cross(rd, T.e2);
    float det = dot(T.e1, pvec);
    if (abs(det) < 1e-8) return false;
    float invDet = 1.0 / det;
    vec3 tvec = ro - T.v0;
    float u = dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) return false;
    vec3 qvec = cross(tvec, T.e1);
    float v = dot(rd, qvec) * invDet;
    if (v < 0.0 || u + v > 1.0) return false;
    float tt = dot(T.e2, qvec) * invDet;
    if (tt < uEPS || tt > tMax) return false;
    t = tt;
    n = normalize(cross(T.e1, T.e2));
    return true;
}

// -----------------------------------------------------------------------------
// BVH traversal (closest-hit)
// -----------------------------------------------------------------------------

/**
 * @brief Traverses the BVH to find the closest triangle hit.
 *
 * Uses an explicit stack-based traversal (no recursion) and tests nodes
 * front-to-back with simple near-ordering between left/right children.
 *
 * On success, hitOut is filled with:
 *  - t: closest hit distance
 *  - p: hit position
 *  - n: shading normal
 *  - mat: material index (triangles currently treated as diffuse = 1)
 *
 * @param ro      Ray origin in world space.
 * @param rd      Ray direction (normalized).
 * @param hitOut  Output Hit structure.
 * @return True if any triangle was hit, false otherwise.
 */
bool traceBVH(vec3 ro, vec3 rd, out Hit hitOut) {
    if (uNodeCount <= 0 || uTriCount <= 0) return false;
    hitOut.t = uINF;
    hitOut.n = vec3(0);
    hitOut.mat = 1; // diffuse default
    float tminBox, tmaxBox;
    vec3 rdInv = 1.0 / rd;

    int stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        int ni = stack[--sp];
        NodeSOA N = nodeFetch(ni);
        if (!aabbHit(ro, rdInv, N.bmin, N.bmax, tminBox, tmaxBox) || tminBox > hitOut.t) continue;

        if (N.count > 0) {
            // Leaf: test all triangles in [first, first + count)
            for (int i = 0; i < N.count; ++i) {
                int triIdx = N.first + i;
                TriSOA T = triFetch(triIdx);
                float t;
                vec3 n;
                if (triHit(ro, rd, T, hitOut.t, t, n)) {
                    hitOut.t = t;
                    hitOut.p = ro + rd * t;
                    hitOut.n = n;
                    hitOut.mat = 1; // triangles = diffuse
                }
            }
        } else {
            // Inner node: test children, push them in near/far order
            NodeSOA L = nodeFetch(N.left);
            NodeSOA R = nodeFetch(N.right);
            float tminL, tmaxL, tminR, tmaxR;
            bool hitL = aabbHit(ro, rdInv, L.bmin, L.bmax, tminL, tmaxL) && tminL <= hitOut.t;
            bool hitR = aabbHit(ro, rdInv, R.bmin, R.bmax, tminR, tmaxR) && tminR <= hitOut.t;
            if (hitL && hitR) {
                bool leftFirst = tminL < tminR;
                stack[sp++] = leftFirst ? N.right : N.left;
                stack[sp++] = leftFirst ? N.left : N.right;
            } else if (hitL) {
                stack[sp++] = N.left;
            } else if (hitR) {
                stack[sp++] = N.right;
            }
        }
    }
    return hitOut.t < uINF;
}

// -----------------------------------------------------------------------------
// BVH traversal (shadow ray, early-out)
// -----------------------------------------------------------------------------

/**
 * @brief Shadow ray traversal with early-out.
 *
 * Similar to traceBVH(), but only checks whether **any** triangle is hit
 * before a maximum distance tMax. Used for hard shadow tests against the BVH.
 *
 * @param ro   Ray origin.
 * @param rd   Ray direction (normalized).
 * @param tMax Maximum distance (e.g., distance to light).
 * @return True if the ray is occluded by any triangle before tMax.
 */
bool traceBVHShadow(vec3 ro, vec3 rd, float tMax) {
    if (uNodeCount <= 0 || uTriCount <= 0) return false; // no occluders
    float tminBox, tmaxBox;
    vec3 rdInv = 1.0 / rd;

    int stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        int ni = stack[--sp];
        NodeSOA N = nodeFetch(ni);
        if (!aabbHit(ro, rdInv, N.bmin, N.bmax, tminBox, tmaxBox) || tminBox > tMax) continue;

        if (N.count > 0) {
            // Leaf: any triangle hit before tMax means occlusion.
            for (int i = 0; i < N.count; ++i) {
                int triIdx = N.first + i;
                TriSOA T = triFetch(triIdx);
                float t;
                vec3 n;
                if (triHit(ro, rd, T, tMax, t, n)) {
                    return true; // any hit before light → occluded
                }
            }
        } else {
            // Inner node: visit potentially intersected children.
            NodeSOA L = nodeFetch(N.left);
            NodeSOA R = nodeFetch(N.right);
            float tminL, tmaxL, tminR, tmaxR;
            bool hitL = aabbHit(ro, rdInv, L.bmin, L.bmax, tminL, tmaxL) && tminL <= tMax;
            bool hitR = aabbHit(ro, rdInv, R.bmin, R.bmax, tminR, tmaxR) && tminR <= tMax;
            if (hitL && hitR) {
                bool leftFirst = tminL < tminR;
                stack[sp++] = leftFirst ? N.right : N.left;
                stack[sp++] = leftFirst ? N.left : N.right;
            } else if (hitL) {
                stack[sp++] = N.left;
            } else if (hitR) {
                stack[sp++] = N.right;
            }
        }
    }
    return false;
}

#endif // RT_BVH_GLSL