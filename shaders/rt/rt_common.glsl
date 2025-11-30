// rt_common.glsl
#ifndef RT_COMMON_GLSL
#define RT_COMMON_GLSL

/*
    rt_common.glsl – Shared Ray Tracing Utilities

    This module provides:
    - Core constants and small configuration macros.
    - Hit payload structure used by all ray/path tracing routines.
    - Hash-based RNG utilities (rand) for per-pixel / per-frame sampling.
    - Halton-based low-discrepancy sequence (ld2) used for jitter.
    - Concentric disk sampling for soft shadows / area lights.
    - Helpers to convert world-space positions to NDC (for motion vectors).

    These helpers are intentionally lightweight and stateless so they can be
    reused across analytic, BVH, lighting, and TAA code.
*/

// ---- Params / constants ----

/// Number of soft-shadow samples for area-light integration.
#define SOFT_SHADOW_SAMPLES 4

/// Enables mirror bounces in the path tracer when set to 1.
#define ENABLE_MIRROR_BOUNCE 1

// ---------------- Hit payload ----------------

/**
 * @brief Hit payload shared by analytic and BVH tracing.
 *
 * Fields:
 *  - t   : distance along the ray to the hit point
 *  - p   : world-space hit position
 *  - n   : world-space shading normal
 *  - mat : material identifier (used to dispatch shading)
 */
struct Hit {
    float t;
    vec3 p;
    vec3 n;
    int mat;
};

// -------- Random & helpers ----------

/**
 * @brief Simple 2D integer hash (32-bit) for RNG.
 *
 * Based on a small LCG-style mixing sequence. Used as a building block
 * for float-valued random numbers.
 *
 * @param v Input 2D unsigned integer vector.
 * @return Pseudo-random 32-bit unsigned int.
 */
uint hash2(uvec2 v) {
    v = v * 1664525u + 1013904223u;
    v.x ^= v.y >> 16;
    v.y ^= v.x << 5;
    v = v * 1664525u + 1013904223u;
    return v.x ^ v.y;
}

/**
 * @brief Float RNG in [0,1) based on integer hashing.
 *
 * Combines pixel coordinates and frame index to produce a reproducible
 * but decorrelated random value.
 *
 * @param p     2D position (typically pixel coords).
 * @param frame Frame index used to vary the pattern over time.
 * @return Pseudo-random float in [0,1).
 */
float rand(vec2 p, int frame) {
    return float(hash2(uvec2(p) ^ uvec2(frame, frame * 1663))) / 4294967296.0;
}

/**
 * @brief Distance-dependent epsilon helper.
 *
 * Used to generate a numerical tolerance that grows slightly with
 * distance to reduce self-intersection artifacts.
 *
 * @param d Distance measure (e.g., hit distance).
 * @return Epsilon value appropriate for that scale.
 */
float epsForDist(float d) {
    return max(1e-4, 1e-3 * d);
}

// -----------------------------------------------------------------------------
// Halton (low-discrepancy per-frame jitter)
// -----------------------------------------------------------------------------

/**
 * @brief Computes the i-th sample of a 1D Halton sequence base b.
 *
 * Used to generate quasi-random samples with better stratification than
 * pure random numbers, especially for TAA jitter and sampling patterns.
 *
 * @param i Sample index.
 * @param b Base of the Halton sequence (e.g., 2 or 3).
 * @return Halton value in [0,1).
 */
float halton(int i, int b) {
    float f = 1.0;
    float r = 0.0;
    int n = i;
    while (n > 0) {
        f /= float(b);
        r += f * float(n % b);
        n /= b;
    }
    return r;
}

/**
 * @brief 2D low-discrepancy sequence using Halton bases 2 and 3.
 *
 * ld2(i) = (Halton(i+1, 2), Halton(i+1, 3)).
 * Useful for jitter patterns and sampling across frames.
 *
 * @param i Sample index.
 * @return 2D low-discrepancy sample in [0,1)^2.
 */
vec2 ld2(int i) {
    return vec2(halton(i + 1, 2), halton(i + 1, 3));
}

// -----------------------------------------------------------------------------
// Disk sample (area light)
// -----------------------------------------------------------------------------

/**
 * @brief Concentric mapping from [0,1]^2 to a unit disk.
 *
 * Converts a uniform 2D random variable into a uniform disk sample using
 * the "concentric mapping" technique (better distribution than naive polar).
 *
 * @param u 2D uniform sample in [0,1]^2.
 * @return 2D sample on a unit disk (x,y).
 */
vec2 concentricSample(vec2 u) {
    float a = 2.0 * u.x - 1.0;
    float b = 2.0 * u.y - 1.0;
    float r, phi;
    if (a == 0.0 && b == 0.0) {
        r = 0.0;
        phi = 0.0;
    } else if (abs(a) > abs(b)) {
        r = a;
        phi = (uPI / 4.0) * (b / a);
    } else {
        r = b;
        phi = (uPI / 2.0) - (uPI / 4.0) * (a / b);
    }
    return r * vec2(cos(phi), sin(phi));
}

// -----------------------------------------------------------------------------
// Motion helpers
// -----------------------------------------------------------------------------

/**
 * @brief Projects a world-space position into NDC.
 *
 * Used to compute motion vectors (currentNDC - prevNDC) by applying
 * the current and previous view-projection matrices.
 *
 * @param p  World-space position.
 * @param VP View-projection matrix.
 * @return NDC coordinates in [-1,1]^2.
 */
vec2 ndcFromWorld(vec3 p, mat4 VP) {
    vec4 clip = VP * vec4(p, 1.0);
    vec3 ndc = clip.xyz / max(clip.w, 1e-6);
    return ndc.xy; // [-1,1]
}

#endif // RT_COMMON_GLSL