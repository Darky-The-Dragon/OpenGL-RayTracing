// rt_common.glsl
#ifndef RT_COMMON_GLSL
#define RT_COMMON_GLSL

// ---- Params / constants ----
#define SOFT_SHADOW_SAMPLES 4
#define ENABLE_MIRROR_BOUNCE 1

const float EPS = 1e-4;
const float PI  = 3.1415926535;
const float INF = 1e30;

// ---------------- Hit payload ----------------
struct Hit {
    float t;
    vec3  p;
    vec3  n;
    int   mat;
};

// -------- Random & helpers ----------
uint hash2(uvec2 v) {
    v = v * 1664525u + 1013904223u;
    v.x ^= v.y >> 16;
    v.y ^= v.x << 5;
    v = v * 1664525u + 1013904223u;
    return v.x ^ v.y;
}

float rand(vec2 p, int frame) {
    return float(hash2(uvec2(p) ^ uvec2(frame, frame * 1663))) / 4294967296.0;
}

float epsForDist(float d) {
    return max(1e-4, 1e-3 * d);
}

// Halton (low-discrepancy per-frame jitter)
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

vec2 ld2(int i) {
    return vec2(halton(i + 1, 2), halton(i + 1, 3));
}

// ---- Disk sample (area light)
vec2 concentricSample(vec2 u) {
    float a = 2.0 * u.x - 1.0;
    float b = 2.0 * u.y - 1.0;
    float r, phi;
    if (a == 0.0 && b == 0.0) {
        r = 0.0;
        phi = 0.0;
    } else if (abs(a) > abs(b)) {
        r = a;
        phi = (PI / 4.0) * (b / a);
    } else {
        r = b;
        phi = (PI / 2.0) - (PI / 4.0) * (a / b);
    }
    return r * vec2(cos(phi), sin(phi));
}

// ---- Motion helpers
vec2 ndcFromWorld(vec3 p, mat4 VP) {
    vec4 clip = VP * vec4(p, 1.0);
    vec3 ndc  = clip.xyz / max(clip.w, 1e-6);
    return ndc.xy; // [-1,1]
}

#endif // RT_COMMON_GLSL