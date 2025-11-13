// rt_bvh.glsl
#ifndef RT_BVH_GLSL
#define RT_BVH_GLSL

// -------- BVH fetch helpers ----------
struct TriSOA {
    vec3 v0;
    vec3 e1;
    vec3 e2;
};

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

struct NodeSOA {
    vec3 bmin; int left;
    vec3 bmax; int right;
    int first; int count;
};

NodeSOA nodeFetch(int nodeIdx) {
    int base = nodeIdx * 3;
    vec4 n0 = texelFetch(uBvhNodes, base + 0);
    vec4 n1 = texelFetch(uBvhNodes, base + 1);
    vec4 n2 = texelFetch(uBvhNodes, base + 2);
    NodeSOA N;
    N.bmin  = n0.xyz; N.left  = int(n0.w + 0.5);
    N.bmax  = n1.xyz; N.right = int(n1.w + 0.5);
    N.first = int(n2.x + 0.5);
    N.count = int(n2.y + 0.5);
    return N;
}

// Ray-AABB
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

// Ray-tri Möller–Trumbore with precomputed v0,e1,e2
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
    if (tt < EPS || tt > tMax) return false;
    t = tt;
    n = normalize(cross(T.e1, T.e2));
    return true;
}

// BVH traversal (closest-hit)
bool traceBVH(vec3 ro, vec3 rd, out Hit hitOut) {
    if (uNodeCount <= 0 || uTriCount <= 0) return false;
    hitOut.t = INF;
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
            for (int i = 0; i < N.count; ++i) {
                int triIdx = N.first + i;
                TriSOA T = triFetch(triIdx);
                float t;
                vec3  n;
                if (triHit(ro, rd, T, hitOut.t, t, n)) {
                    hitOut.t   = t;
                    hitOut.p   = ro + rd * t;
                    hitOut.n   = n;
                    hitOut.mat = 1; // triangles = diffuse
                }
            }
        } else {
            NodeSOA L = nodeFetch(N.left);
            NodeSOA R = nodeFetch(N.right);
            float tminL, tmaxL, tminR, tmaxR;
            bool hitL = aabbHit(ro, rdInv, L.bmin, L.bmax, tminL, tmaxL) && tminL <= hitOut.t;
            bool hitR = aabbHit(ro, rdInv, R.bmin, R.bmax, tminR, tmaxR) && tminR <= hitOut.t;
            if (hitL && hitR) {
                bool leftFirst = tminL < tminR;
                stack[sp++] = leftFirst ? N.right : N.left;
                stack[sp++] = leftFirst ? N.left  : N.right;
            } else if (hitL) {
                stack[sp++] = N.left;
            } else if (hitR) {
                stack[sp++] = N.right;
            }
        }
    }
    return hitOut.t < INF;
}

// BVH traversal (shadow ray, early-out)
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
            for (int i = 0; i < N.count; ++i) {
                int triIdx = N.first + i;
                TriSOA T = triFetch(triIdx);
                float t;
                vec3  n;
                if (triHit(ro, rd, T, tMax, t, n)) {
                    return true; // any hit before light → occluded
                }
            }
        } else {
            NodeSOA L = nodeFetch(N.left);
            NodeSOA R = nodeFetch(N.right);
            float tminL, tmaxL, tminR, tmaxR;
            bool hitL = aabbHit(ro, rdInv, L.bmin, L.bmax, tminL, tmaxL) && tminL <= tMax;
            bool hitR = aabbHit(ro, rdInv, R.bmin, R.bmax, tminR, tmaxR) && tminR <= tMax;
            if (hitL && hitR) {
                bool leftFirst = tminL < tminR;
                stack[sp++] = leftFirst ? N.right : N.left;
                stack[sp++] = leftFirst ? N.left  : N.right;
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