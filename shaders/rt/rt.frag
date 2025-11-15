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
    int  SPP      = max(uSpp, 1);

    // default motion = 0 (miss)
    vec2 motionOut = vec2(0.0);

    // --- Per-frame camera jitter (constant within the frame)
    vec2 camJit = ld2(uFrameIndex) - 0.5;

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
                const float giScaleBVH      = 0.25;  // a bit lower, BVH is noisier

                if (uUseBVH == 1) {
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
            radiance = sky(dir);
        }

        // -----------------------------------------------------------------
        // Final per-sample clamp to avoid fireflies / insane outliers
        // -----------------------------------------------------------------
        // You can tweak this. Try 8.0–15.0 depending on how bright you want things.
        const float RADIANCE_CLAMP = 10.0;
        radiance = clamp(radiance, vec3(0.0), vec3(RADIANCE_CLAMP));

        frameSum += radiance;
    }

    // Average current frame samples
    vec3 curr = frameSum / float(SPP);

    // Current UV (what we are shading now)
    vec2 uvCurr = vUV;

    // TAA + TVE (variance in alpha)
    vec4 taaAndVar = resolveTAA(curr, uvCurr, motionOut, uPrevAccum, uFrameIndex);

    fragColor = taaAndVar;      // rgb = color, a = variance
    outMotion = motionOut;      // for debug / future passes
}