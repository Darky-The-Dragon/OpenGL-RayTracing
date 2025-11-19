#version 410 core
in vec2 vUV;
layout (location = 0) out vec4 fragColor;   // accumulated linear color
layout (location = 1) out vec2 outMotion;   // NDC motion (currentNDC - prevNDC)

// ---- Camera & accumulation
uniform vec3 uCamPos;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamFwd;
uniform float uTanHalfFov;
uniform float uAspect;
uniform int uFrameIndex;
uniform vec2 uResolution;
uniform sampler2D uPrevAccum;
uniform int uSpp;

// ---- Scene mode
uniform int uUseBVH;     // 0 = analytic (plane+spheres), 1 = BVH triangle scene
uniform int uNodeCount;
uniform int uTriCount;

// ---- BVH TBOs (only used if uUseBVH==1)
uniform samplerBuffer uBvhNodes;
uniform samplerBuffer uBvhTris;

// ---- Motion debug (F6)
uniform int uShowMotion;               // 0 = normal, 1 = visualize motion (present-time)
uniform mat4 uPrevViewProj;
uniform mat4 uCurrViewProj;
uniform int uCameraMoved;             // 0 = camera is static, 1 = camera is moving

// Include modular shader chunks.
// NOTE: these requires CPU-side preprocessing / concatenation in OpenGL 4.1.
#include "rt_common.glsl"
#include "rt_materials.glsl"
#include "rt_scene_analytic.glsl"
#include "rt_bvh.glsl"
#include "rt_lighting.glsl"
#include "rt_taa.glsl"

// ================== MAIN ==================
void main() {
    vec3 frameSum = vec3(0.0);
    int SPP = max(uSpp, 1);

    // Default motion for this pixel:
    // - If we MISS and camera is static, we want motion = 0 (allow TAA to accumulate).
    // - If we MISS and camera moved, we'll override it to a large value so TAA kills history.
    vec2 motionOut = vec2(0.0);

    // --- Per-frame camera jitter (reduced amplitude to cut residual shimmer)
    vec2 camJit = (ld2(uFrameIndex) - 0.5) * 0.5;   // half-pixel jitter

    for (int s = 0; s < SPP; ++s) {
        // keep seed unique per frame & optional per-sample
        int seed = uFrameIndex * max(1, SPP) + s;

        // use per-frame jitter (stable within frame)
        vec2 uv = (gl_FragCoord.xy + camJit) / uResolution;
        vec2 ndc = uv * 2.0 - 1.0;

        vec3 dir = normalize(
            uCamFwd
            + ndc.x * uCamRight * (uTanHalfFov * uAspect)
            + ndc.y * uCamUp * uTanHalfFov
        );

        // Choose scene
        Hit h;
        bool hitAny = (uUseBVH == 1)
        ? traceBVH(uCamPos, dir, h)
        : traceAnalytic(uCamPos, dir, h);

        vec3 radiance;
        if (hitAny) {
            // Only write motion once (first sample) to keep it stable
            if (s == 0) {
                vec2 pN = ndcFromWorld(h.p, uPrevViewProj);
                vec2 cN = ndcFromWorld(h.p, uCurrViewProj);
                motionOut = cN - pN;
            }

            // View direction from hit to camera
            vec3 V = -dir;

            // Primary lighting: direct from area light
            radiance = (uUseBVH == 1)
            ? directLightBVH(h, seed, V)
            : directLight(h, seed, V);

            // -----------------------------------------------------------------
            // One-bounce diffuse GI (indirect lighting)
            // -----------------------------------------------------------------
            {
                const float giScaleAnalytic = 0.35;
                const float giScaleBVH = 0.20;

                if (uUseBVH == 1) {
                    // BVH scene GI (more conservative)
                    radiance += giScaleBVH * oneBounceGIBVH(h, uFrameIndex, seed);
                } else {
                    MaterialProps m0 = getMaterial(h.mat);
                    if (m0.type == 0) { // skip mirrors for GI
                                        radiance += giScaleAnalytic * oneBounceGIAnalytic(h, uFrameIndex, seed);
                    }
                }
            }

            #if ENABLE_MIRROR_BOUNCE
            if (h.mat == 3) {
                vec3 rdir = reflect(dir, h.n);
                // offset along reflection direction to avoid self-hit acne
                vec3 rorg = h.p + rdir * EPS;

                Hit h2;
                bool hit2 = (uUseBVH == 1)
                ? traceBVH(rorg, rdir, h2)
                : traceAnalytic(rorg, rdir, h2);

                if (hit2) {
                    vec3 V2 = -rdir;
                    vec3 bounced = (uUseBVH == 1)
                    ? directLightBVH(h2, seed, V2)
                    : directLight(h2, seed, V2);

                    radiance += 0.9 * bounced;
                } else {
                    radiance += 0.9 * sky(rdir);
                }
            }
            #endif

            // -----------------------------------------------------------------
            // Ambient Occlusion modulation (subtle)
            // -----------------------------------------------------------------
            float ao = computeAO(h, uFrameIndex);
            radiance *= ao;

        } else {
            // MISS → sky
            radiance = sky(dir);

            // If the camera actually moved this frame, we want this pixel to
            // be treated as a disocclusion in TAA, to avoid "sphere→sky" smearing.
            if (uCameraMoved == 1 && s == 0) {
                motionOut = vec2(4.0, 4.0); // large NDC motion -> uvPrev OOB -> no history
            }
        }

        frameSum += radiance;
    }

    // Average current frame samples
    vec3 curr = frameSum / float(SPP);

    // Current UV
    vec2 uvCurr = vUV;

    // Effective motion for TAA:
    // - if camera really moved: use motionOut
    // - if camera is "still": ignore motion so we hit the still-case path
    vec2 taaMotion = (uCameraMoved == 1) ? motionOut : vec2(0.0);

    vec4 taa = resolveTAA(curr, uvCurr, taaMotion, uPrevAccum, uFrameIndex);

    fragColor = taa;        // rgb = color, a unused
    outMotion = motionOut;  // full motion kept for debug view (F6)
}