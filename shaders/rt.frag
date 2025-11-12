#version 410 core
in vec2 vUV;
layout(location = 0) out vec4 fragColor;   // accumulated linear color
layout(location = 1) out vec2 outMotion;   // NDC motion (currentNDC - prevNDC)

// ---- Camera & accumulation
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanHalfFov;
uniform float uAspect;
uniform int   uFrameIndex;
uniform vec2  uResolution;
uniform sampler2D uPrevAccum;
uniform int   uSpp;

// ---- Scene mode
uniform int   uUseBVH;     // 0 = analytic (plane+spheres), 1 = BVH triangle scene
uniform int   uNodeCount;
uniform int   uTriCount;

// ---- BVH TBOs (only used if uUseBVH==1)
uniform samplerBuffer uBvhNodes;
uniform samplerBuffer uBvhTris;

// ---- Motion debug (F6)
uniform int   uShowMotion;               // 0 = normal, 1 = visualize motion (present-time)
uniform mat4  uPrevViewProj;
uniform mat4  uCurrViewProj;

// ---- Params
#define SOFT_SHADOW_SAMPLES 4
#define ENABLE_MIRROR_BOUNCE 1
const float EPS = 1e-4;
const float PI  = 3.1415926535;
const float INF = 1e30;

// ---------------- Materials (analytic) -------------
struct Hit { float t; vec3 p; vec3 n; int mat; };
vec3 materialAlbedo(int id) {
    if (id == 1) return vec3(0.85, 0.25, 0.25);
    if (id == 2) return vec3(0.25, 0.85, 0.35);
    if (id == 3) return vec3(0.95); // mirror
    return vec3(0.8);
}

// -------- Analytic intersections --------
bool intersectPlane(vec3 ro, vec3 rd, vec3 n, float d, out Hit h, int matId) {
    float denom = dot(n, rd);
    if (abs(denom) < 1e-6) return false;
    float t = -(dot(n, ro) + d) / denom;
    if (t < EPS) return false;
    h.t = t; h.p = ro + rd * t; h.n = n; h.mat = matId; return true;
}
bool intersectSphere(vec3 ro, vec3 rd, vec3 c, float r, out Hit h, int matId) {
    vec3 oc = ro - c;
    float b = dot(oc, rd);
    float c2 = dot(oc, oc) - r * r;
    float disc = b * b - c2;
    if (disc < 0.0) return false;
    float s = sqrt(disc);
    float t = -b - s; if (t < EPS) t = -b + s; if (t < EPS) return false;
    h.t = t; h.p = ro + rd * t; h.n = normalize(h.p - c); h.mat = matId; return true;
}
bool traceAnalytic(vec3 ro, vec3 rd, out Hit hit) {
    hit.t = INF; Hit h;
    if (intersectPlane(ro, rd, vec3(0, 1, 0), 0.0, h, 0) && h.t < hit.t) hit = h;
    if (intersectSphere(ro, rd, vec3(-1.2, 1.0, -3.5), 1.0, h, 1) && h.t < hit.t) hit = h;
    if (intersectSphere(ro, rd, vec3(1.2, 0.7, -2.5), 0.7, h, 3) && h.t < hit.t) hit = h;
    return hit.t < INF;
}

// -------- Sky ----------
vec3 sky(vec3 d) {
    float t = clamp(0.5 * (d.y + 1.0), 0.0, 1.0);
    return mix(vec3(0.7, 0.8, 1.0) * 0.6, vec3(0.1, 0.2, 0.5), 1.0 - t);
}

// -------- Random & helpers ----------
uint hash2(uvec2 v) {
    v = v * 1664525u + 1013904223u;
    v.x ^= v.y >> 16; v.y ^= v.x << 5;
    v = v * 1664525u + 1013904223u;
    return v.x ^ v.y;
}
float rand(vec2 p, int frame) {
    return float(hash2(uvec2(p) ^ uvec2(frame, frame * 1663))) / 4294967296.0;
}
float epsForDist(float d) { return max(1e-4, 1e-3 * d); }

// Halton (low-discrepancy per-frame jitter)
float halton(int i, int b) {
    float f = 1.0, r = 0.0;
    int n = i;
    while (n > 0) { f /= float(b); r += f * float(n % b); n /= b; }
    return r;
}
vec2 ld2(int i) { return vec2(halton(i + 1, 2), halton(i + 1, 3)); }

// ---- Disk sample (area light)
vec2 concentricSample(vec2 u) {
    float a = 2.0 * u.x - 1.0; float b = 2.0 * u.y - 1.0;
    float r, phi;
    if (a == 0.0 && b == 0.0) { r = 0.0; phi = 0.0; }
    else if (abs(a) > abs(b)) { r = a; phi = (PI / 4.0) * (b / a); }
    else { r = b; phi = (PI / 2.0) - (PI / 4.0) * (a / b); }
    return r * vec2(cos(phi), sin(phi));
}

// ------------- Light --------------
const vec3  kLightCenter = vec3(0.0, 5.0, -3.0);
const vec3  kLightN      = normalize(vec3(0.0, -1.0, 0.2));
const float kLightRadius = 1.2;
const vec3  kLightCol    = vec3(18.0);

// -------- BVH fetch helpers ----------
struct TriSOA { vec3 v0; vec3 e1; vec3 e2; };
TriSOA triFetch(int triIdx) {
    int base = triIdx * 3;
    vec4 t0 = texelFetch(uBvhTris, base + 0);
    vec4 t1 = texelFetch(uBvhTris, base + 1);
    vec4 t2 = texelFetch(uBvhTris, base + 2);
    TriSOA T; T.v0 = t0.xyz; T.e1 = t1.xyz; T.e2 = t2.xyz; return T;
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
    N.bmin = n0.xyz; N.left = int(n0.w + 0.5);
    N.bmax = n1.xyz; N.right = int(n1.w + 0.5);
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
    tminOut = tmin; tmaxOut = tmax;
    return tmax >= tmin;
}

// Ray-tri Möller–Trumbore with precomputed v0,e1,e2
bool triHit(vec3 ro, vec3 rd, TriSOA T, float tMax, out float t, out vec3 n) {
    vec3 pvec = cross(rd, T.e2);
    float det = dot(T.e1, pvec);
    if (abs(det) < 1e-8) return false;
    float invDet = 1.0 / det;
    vec3 tvec = ro - T.v0;
    float u = dot(tvec, pvec) * invDet; if (u < 0.0 || u > 1.0) return false;
    vec3 qvec = cross(tvec, T.e1);
    float v = dot(rd, qvec) * invDet; if (v < 0.0 || u + v > 1.0) return false;
    float tt = dot(T.e2, qvec) * invDet; if (tt < EPS || tt > tMax) return false;
    t = tt;
    n = normalize(cross(T.e1, T.e2));
    return true;
}

// BVH traversal (closest-hit)
bool traceBVH(vec3 ro, vec3 rd, out Hit hitOut) {
    if (uNodeCount <= 0 || uTriCount <= 0) return false;
    hitOut.t = INF; hitOut.n = vec3(0); hitOut.mat = 1; // diffuse default
    float tminBox, tmaxBox;
    vec3 rdInv = 1.0 / rd;

    int stack[64];
    int sp = 0; stack[sp++] = 0;

    while (sp > 0) {
        int ni = stack[--sp];
        NodeSOA N = nodeFetch(ni);
        if (!aabbHit(ro, rdInv, N.bmin, N.bmax, tminBox, tmaxBox) || tminBox > hitOut.t) continue;

        if (N.count > 0) {
            for (int i = 0; i < N.count; ++i) {
                int triIdx = N.first + i;
                TriSOA T = triFetch(triIdx);
                float t; vec3 n;
                if (triHit(ro, rd, T, hitOut.t, t, n)) {
                    hitOut.t = t;
                    hitOut.p = ro + rd * t;
                    hitOut.n = n;
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
                stack[sp++] = leftFirst ? N.left : N.right;
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
    int sp = 0; stack[sp++] = 0;

    while (sp > 0) {
        int ni = stack[--sp];
        NodeSOA N = nodeFetch(ni);
        if (!aabbHit(ro, rdInv, N.bmin, N.bmax, tminBox, tmaxBox) || tminBox > tMax) continue;

        if (N.count > 0) {
            for (int i = 0; i < N.count; ++i) {
                int triIdx = N.first + i;
                TriSOA T = triFetch(triIdx);
                float t; vec3 n;
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

// ---- Unified shadow test for both modes
bool occludedToward(vec3 p, vec3 q) {
    vec3 rd = normalize(q - p);
    float maxT = length(q - p);
    float eps = epsForDist(maxT);
    if (uUseBVH == 1) {
        return traceBVHShadow(p + rd * eps, rd, maxT - eps);
    } else {
        Hit h;
        if (traceAnalytic(p + rd * eps, rd, h) && h.t < maxT - eps) return true;
        return false;
    }
}

// ---- Direct lighting (analytic & BVH) with per-pixel CP rotation + frame LD rotation
vec2 cpOffset(vec2 pix, int frame) {
    // Per-pixel hash → [0,1)^2
    vec2 h = vec2(
    rand(pix, frame * 911),
    rand(pix.yx, frame * 577)
    );
    // Per-frame low-discrepancy rotation
    vec2 ld = ld2(frame);
    return fract(h + ld);
}

vec3 directLight(Hit h, int frame) {
    vec3 N = h.n;
    vec3 sum = vec3(0.0);

    // Orthonormal basis for disk light
    vec3 t = normalize(abs(kLightN.y) < 0.99 ? cross(kLightN, vec3(0, 1, 0)) : cross(kLightN, vec3(1, 0, 0)));
    vec3 b = cross(kLightN, t);

    vec2 rot = cpOffset(gl_FragCoord.xy, uFrameIndex);

    for (int i = 0; i < SOFT_SHADOW_SAMPLES; ++i) {
        // base random
        vec2 u = vec2(
        rand(gl_FragCoord.xy + float(i), frame),
        rand(gl_FragCoord.yx + float(31 * i + 7), frame)
        );
        // Cranley–Patterson rotation + wrap
        u = fract(u + rot);

        vec2 d = concentricSample(u) * kLightRadius;
        vec3 xL = kLightCenter + t * d.x + b * d.y;

        vec3 L  = normalize(xL - h.p);
        float ndl = max(dot(N, L), 0.0);
        float cosThetaL = max(dot(-kLightN, L), 0.0);
        float r2 = max(dot(xL - h.p, xL - h.p), 1e-4);
        float geom = (ndl * cosThetaL) / r2;
        float vis  = occludedToward(h.p, xL) ? 0.0 : 1.0;

        sum += materialAlbedo(h.mat) * kLightCol * geom * vis / PI;
    }
    return sum / float(SOFT_SHADOW_SAMPLES);
}

vec3 directLightBVH(Hit h, int frame) {
    vec3 N = h.n;
    vec3 sum = vec3(0.0);

    vec3 t = normalize(abs(kLightN.y) < 0.99 ? cross(kLightN, vec3(0, 1, 0)) : cross(kLightN, vec3(1, 0, 0)));
    vec3 b = cross(kLightN, t);

    vec2 rot = cpOffset(gl_FragCoord.xy, uFrameIndex);

    for (int i = 0; i < SOFT_SHADOW_SAMPLES; ++i) {
        vec2 u = vec2(
        rand(gl_FragCoord.xy + float(i), frame),
        rand(gl_FragCoord.yx + float(31 * i + 7), frame)
        );
        u = fract(u + rot);

        vec2 d = concentricSample(u) * kLightRadius;
        vec3 xL = kLightCenter + t * d.x + b * d.y;

        vec3 L  = normalize(xL - h.p);
        float ndl = max(dot(N, L), 0.0);
        float cosThetaL = max(dot(-kLightN, L), 0.0);
        float r2 = max(dot(xL - h.p, xL - h.p), 1e-4);
        float geom = (ndl * cosThetaL) / r2;
        float vis  = occludedToward(h.p, xL) ? 0.0 : 1.0;

        vec3 albedo = vec3(0.8);
        sum += albedo * kLightCol * geom * vis / PI;
    }
    return sum / float(SOFT_SHADOW_SAMPLES);
}

// ---- Motion helpers
vec2 ndcFromWorld(vec3 p, mat4 VP) {
    vec4 clip = VP * vec4(p, 1.0);
    vec3 ndc  = clip.xyz / max(clip.w, 1e-6);
    return ndc.xy; // [-1,1]
}

// ================== MAIN ==================
void main() {
    vec3 frameSum = vec3(0.0);
    int  SPP = max(uSpp, 1);

    // default motion = 0 (miss)
    vec2 motionOut = vec2(0.0);

    // --- Per-frame camera jitter (constant within the frame)
    vec2 camJit = ld2(uFrameIndex) - 0.5;

    for (int s = 0; s < SPP; ++s) {
        // keep seed unique per frame & optional per-sample
        int seed = uFrameIndex * max(1, SPP) + s;

        // use per-frame jitter (stable within frame)
        vec2 uv  = (gl_FragCoord.xy + camJit) / uResolution;
        vec2 ndc = uv * 2.0 - 1.0;

        vec3 dir = normalize(
            uCamFwd
            + ndc.x * uCamRight * (uTanHalfFov * uAspect)
            + ndc.y * uCamUp    *  uTanHalfFov
        );

        // Choose scene
        Hit h;
        bool hitAny = (uUseBVH == 1) ? traceBVH(uCamPos, dir, h)
        : traceAnalytic(uCamPos, dir, h);

        vec3 radiance;
        if (hitAny) {
            // compute current/prev NDC for the hit point (primary hit)
            vec2 pN = ndcFromWorld(h.p, uPrevViewProj);
            vec2 cN = ndcFromWorld(h.p, uCurrViewProj);
            motionOut = cN - pN;

            radiance = (uUseBVH == 1) ? directLightBVH(h, seed)
            : directLight(h,    seed);

            #if ENABLE_MIRROR_BOUNCE
            if (h.mat == 3) {
                vec3 rdir = reflect(dir, h.n);
                // offset along reflection direction to avoid self-hit acne
                vec3 rorg = h.p + rdir * EPS;
                Hit h2; bool hit2 = (uUseBVH == 1) ? traceBVH(rorg, rdir, h2)
                : traceAnalytic(rorg, rdir, h2);
                radiance += hit2 ? (0.9 * ((uUseBVH==1)?directLightBVH(h2, seed):directLight(h2, seed)))
                : (0.9 * sky(rdir));
            }
            #endif
        } else {
            radiance = sky(dir);
        }

        frameSum += radiance;
    }

    vec3 frameAvg = frameSum / float(SPP);

    // Normal accumulation in linear space
    vec3 prev = texture(uPrevAccum, vUV).rgb;
    float n   = float(uFrameIndex);
    vec3 accum = (prev * n + frameAvg) / (n + 1.0);

    // Write outputs
    fragColor = vec4(accum, 1.0);
    outMotion = motionOut;  // NDC delta; present can visualize when F6 is ON
}s