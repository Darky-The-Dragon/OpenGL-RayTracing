#version 410 core

in vec2 vUV;
out vec4 fragColor;

// --- Uniforms from C++ (must match your main.cpp) ---
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanHalfFov;
uniform float uAspect;
uniform int   uFrameIndex;
uniform vec2  uResolution;
uniform sampler2D uPrevAccum;

// ----------- Params you can tweak -----------
#define SOFT_SHADOW_SAMPLES 4      // spp per frame toward area light (converges over time)
#define ENABLE_MIRROR_BOUNCE 1     // 1 = one perfect reflection bounce
const float EPS = 1e-4;
const float PI  = 3.1415926535;
const float INF = 1e30;

// ----------- Scene & materials -----------
struct Hit { float t; vec3 p; vec3 n; int mat; };

// Materials: 0=diffuse gray, 1=red, 2=green, 3=mirror
vec3 materialAlbedo(int id) {
    if (id == 1) return vec3(0.85, 0.25, 0.25);
    if (id == 2) return vec3(0.25, 0.85, 0.35);
    if (id == 3) return vec3(0.95); // mirror color multiplier
    return vec3(0.8);
}

// Plane: dot(n, x) + d = 0  (assume n normalized)
bool intersectPlane(vec3 ro, vec3 rd, vec3 n, float d, out Hit h, int matId) {
    float denom = dot(n, rd);
    if (abs(denom) < 1e-6) return false;
    float t = -(dot(n, ro) + d) / denom;
    if (t < EPS) return false;
    h.t = t; h.p = ro + rd*t; h.n = n; h.mat = matId; return true;
}

bool intersectSphere(vec3 ro, vec3 rd, vec3 c, float r, out Hit h, int matId) {
    vec3 oc = ro - c;
    float b = dot(oc, rd);
    float c2 = dot(oc, oc) - r*r;
    float disc = b*b - c2;
    if (disc < 0.0) return false;
    float s = sqrt(disc);
    float t = -b - s;
    if (t < EPS) t = -b + s;
    if (t < EPS) return false;
    h.t = t; h.p = ro + rd*t; h.n = normalize(h.p - c); h.mat = matId; return true;
}

bool traceScene(vec3 ro, vec3 rd, out Hit hit) {
    hit.t = INF;
    Hit h;

    // Ground y=0
    if (intersectPlane(ro, rd, vec3(0,1,0), 0.0, h, 0) && h.t < hit.t) hit = h;

    // Left sphere (diffuse red)
    if (intersectSphere(ro, rd, vec3(-1.2, 1.0, -3.5), 1.0, h, 1) && h.t < hit.t) hit = h;

    // Right sphere (mirror)
    if (intersectSphere(ro, rd, vec3( 1.2, 0.7, -2.5), 0.7, h, 3) && h.t < hit.t) hit = h;

    return hit.t < INF;
}

// ----------- Area light (disk) -----------
const vec3  kLightCenter = vec3(0.0, 5.0, -3.0);
const vec3  kLightN      = normalize(vec3(0.0, -1.0, 0.2));
const float kLightRadius = 1.2;
const vec3  kLightCol    = vec3(18.0); // intensity

// Concentric disk mapping
vec2 concentricSample(vec2 u) {
    float a = 2.0*u.x - 1.0;
    float b = 2.0*u.y - 1.0;
    float r, phi;
    if (a == 0.0 && b == 0.0) { r = 0.0; phi = 0.0; }
    else if (abs(a) > abs(b)) { r = a; phi = (PI/4.0) * (b/a); }
    else { r = b; phi = (PI/2.0) - (PI/4.0) * (a/b); }
    return r * vec2(cos(phi), sin(phi));
}

uint hash2(uvec2 v) {
    v = v * 1664525u + 1013904223u;
    v.x ^= v.y >> 16; v.y ^= v.x << 5;
    v = v * 1664525u + 1013904223u;
    return v.x ^ v.y;
}
float rand(vec2 p, int frame) { return float(hash2(uvec2(p) ^ uvec2(frame, frame*1663))) / 4294967296.0; }

// distance-scaled epsilon to reduce self-shadow acne on far tests
float epsForDist(float d) { return max(1e-4, 1e-3 * d); }

bool occludedToward(vec3 p, vec3 q) {
    vec3 rd = normalize(q - p);
    float maxT = length(q - p);
    float eps  = epsForDist(maxT);
    Hit h;
    if (traceScene(p + rd * eps, rd, h) && h.t < maxT - eps) return true;
    return false;
}

// Direct lighting from the disk area light (Next Event Estimation)
vec3 directLight(Hit h, int frame) {
    vec3 N = h.n;
    vec3 sum = vec3(0.0);

    // Light ONB
    vec3 t = normalize(abs(kLightN.y) < 0.99 ? cross(kLightN, vec3(0,1,0)) : cross(kLightN, vec3(1,0,0)));
    vec3 b = cross(kLightN, t);

    for (int i=0; i<SOFT_SHADOW_SAMPLES; ++i) {
        vec2 u = vec2(rand(gl_FragCoord.xy + float(i), frame),
        rand(gl_FragCoord.yx + float(31*i+7), frame));
        vec2 d  = concentricSample(u) * kLightRadius;
        vec3 xL = kLightCenter + t*d.x + b*d.y;

        vec3 L  = normalize(xL - h.p);
        float ndl = max(dot(N, L), 0.0);
        float cosThetaL = max(dot(-kLightN, L), 0.0); // light faces the point

        float r2 = max(dot(xL - h.p, xL - h.p), 1e-4);
        float geom = (ndl * cosThetaL) / r2;

        float vis = occludedToward(h.p, xL) ? 0.0 : 1.0;
        sum += materialAlbedo(h.mat) * kLightCol * geom * vis / PI;
    }
    return sum / float(SOFT_SHADOW_SAMPLES);
}

// ----------- Sky -----------
vec3 sky(vec3 d) {
    float t = clamp(0.5*(d.y + 1.0), 0.0, 1.0);
    return mix(vec3(0.7, 0.8, 1.0)*0.6, vec3(0.1,0.2,0.5), 1.0 - t);
}

// ----------- GI helpers (Option A) -----------
// Stable ONB (Pixar style)
void buildONB(in vec3 n, out vec3 t, out vec3 b) {
    float sign = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + n.z);
    float bb = n.x * n.y * a;
    t = normalize(vec3(1.0 + sign*n.x*n.x*a, sign*bb, -sign*n.x));
    b = normalize(vec3(bb, sign + n.y*n.y*a, -n.y));
}

// Cosine-weighted hemisphere sample in world space
vec3 cosineSampleHemisphere(vec2 u, vec3 n) {
    float r = sqrt(u.x);
    float theta = 6.28318530718 * u.y;
    float x = r * cos(theta), y = r * sin(theta);
    float z = sqrt(max(0.0, 1.0 - x*x - y*y));
    vec3 t,b; buildONB(n,t,b);
    return normalize(x*t + y*b + z*n);
}

// Tiny RNG distinct from rand()
uint hash1(uvec2 v){ v=v*1664525u+1013904223u; v^=v>>16; v=v*1664525u+1013904223u; return v.x^v.y; }
float rand01(uvec2 p){ return float(hash1(p))*2.3283064365386963e-10; }

// One cheap indirect bounce: cosine hemisphere to next hit, then take direct light there
vec3 indirectDiffuseBounce(Hit h, int frame) {
    vec2 xi = vec2(rand01(uvec2(gl_FragCoord.xy) ^ uvec2(frame, 97)),
    rand01(uvec2(gl_FragCoord.yx) ^ uvec2(17, frame*31)));
    vec3 wi = cosineSampleHemisphere(xi, h.n);

    Hit h2;
    if (traceScene(h.p + wi*EPS, wi, h2)) {
        vec3 Li = directLight(h2, frame + 1234);
        return materialAlbedo(h.mat) * Li; // BRDF*cos/pdf simplifies to albedo
    } else {
        vec3 Li = sky(wi);
        return materialAlbedo(h.mat) * Li;
    }
}

// ================== MAIN ==================
void main() {
    // Subpixel jitter (AA that converges over time)
    vec2 jitter = vec2(rand(gl_FragCoord.xy, uFrameIndex),
    rand(gl_FragCoord.yx, uFrameIndex*13)) - 0.5;
    vec2 uv = (gl_FragCoord.xy + jitter) / uResolution;

    // Ray generation
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 dir = normalize(
        uCamFwd +
        ndc.x * uCamRight * (uTanHalfFov * uAspect) +
        ndc.y * uCamUp    *  uTanHalfFov
    );

    Hit  h;
    vec3 radiance = vec3(0.0);

    if (traceScene(uCamPos, dir, h)) {
        // Direct light (soft shadows)
        radiance = directLight(h, uFrameIndex);

        // Add one indirect diffuse bounce for non-mirror surfaces
        if (h.mat != 3) {
            radiance += indirectDiffuseBounce(h, uFrameIndex);
        }

        #if ENABLE_MIRROR_BOUNCE
        if (h.mat == 3) {
            // Single perfect specular bounce
            vec3 rdir = reflect(dir, h.n);
            Hit h2;
            if (traceScene(h.p + h.n*EPS, rdir, h2)) {
                radiance += 0.9 * directLight(h2, uFrameIndex); // 0.9 = mirror tint
            } else {
                radiance += 0.9 * sky(rdir);
            }
        }
        #endif
    } else {
        radiance = sky(dir);
    }

    // -------- Accumulate in LINEAR space --------
    vec3 prev = texture(uPrevAccum, vUV).rgb;
    float n   = float(uFrameIndex);
    vec3 accum = (prev * n + radiance) / (n + 1.0);

    fragColor = vec4(accum, 1.0); // store LINEAR; gamma is applied in rt_present.frag
}
