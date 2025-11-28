#version 410 core
in vec2 vUV;

// COLOR0: accumulated linear color + M2
layout (location = 0) out vec4 fragColor;

// COLOR1: NDC motion (currentNDC - prevNDC)
layout (location = 1) out vec2 outMotion;

// COLOR2: world-space position (xyz, w unused)
layout (location = 2) out vec4 outGPos;

// COLOR3: world-space normal (xyz, w unused)
layout (location = 3) out vec4 outGNrm;

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

// ---- CubeMap
uniform samplerCube uEnvMap;
uniform int uUseEnvMap;
uniform float uEnvIntensity;

// ---- Jitter
uniform vec2 uJitter;
uniform int uEnableJitter;

// ---- Scene mode
uniform int uUseBVH;     // 0 = analytic (plane+spheres), 1 = BVH triangle scene
uniform int uNodeCount;
uniform int uTriCount;

// ---- BVH TBOs (only used if uUseBVH==1)
uniform samplerBuffer uBvhNodes;
uniform samplerBuffer uBvhTris;

// ---- Motion debug (F6)
uniform int uShowMotion;      // 0 = normal, 1 = visualize motion (present-time)
uniform mat4 uPrevViewProj;
uniform mat4 uCurrViewProj;
uniform int uCameraMoved;     // 0 = camera is static, 1 = camera is moving

// ---- TAA params (from RenderParams)
uniform float uTaaStillThresh;
uniform float uTaaHardMovingThresh;
uniform float uTaaHistoryMinWeight;
uniform float uTaaHistoryAvgWeight;
uniform float uTaaHistoryMaxWeight;
uniform float uTaaHistoryBoxSize;
uniform int uEnableTAA;

// ---- GI / AO / mirror params (from RenderParams)
uniform float uGiScaleAnalytic;
uniform float uGiScaleBVH;
uniform int uEnableGI;
uniform int uEnableAO;
uniform int uEnableMirror;
uniform float uMirrorStrength;
uniform int uAO_SAMPLES;
uniform float uAO_RADIUS;
uniform float uAO_BIAS;
uniform float uAO_MIN;

// ---- Constants (from RenderParams)
uniform float uEPS;
uniform float uPI;
uniform float uINF;

// Includes
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
    vec2 motionOut = vec2(0.0);

    // Initialize GBuffer outputs to something safe
    outGPos = vec4(0.0);
    outGNrm = vec4(0.0);

    // --- Per-frame camera jitter
    vec2 camJit = (uEnableJitter == 1) ? uJitter : vec2(0.0);

    for (int s = 0; s < SPP; ++s) {
        int seed = uFrameIndex * max(1, SPP) + s;

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
            // Only write motion + GBuffer once (first sample) to keep them stable
            if (s == 0) {
                vec2 pN = ndcFromWorld(h.p, uPrevViewProj);
                vec2 cN = ndcFromWorld(h.p, uCurrViewProj);
                motionOut = cN - pN;

                outGPos = vec4(h.p, 1.0);
                outGNrm = vec4(normalize(h.n), 0.0);
            }

            vec3 V = -dir; // direction from hit → camera

            if (uUseBVH == 1) {
                // ==========================================================
                // BVH SCENE (unchanged behaviour)
                // ==========================================================
                radiance = directLightBVH(h, seed, V);

                if (uEnableGI == 1) {
                    radiance += uGiScaleBVH * oneBounceGIBVH(h, uFrameIndex, seed);
                }

                #if ENABLE_MIRROR_BOUNCE
                if (uEnableMirror == 1 && h.mat == 3) {
                    vec3 rdir = reflect(dir, h.n);
                    vec3 rorg = h.p + rdir * uEPS;

                    Hit h2;
                    bool hit2 = traceBVH(rorg, rdir, h2);

                    if (hit2) {
                        vec3 V2 = -rdir;
                        vec3 bounced = directLightBVH(h2, seed, V2);
                        radiance += uMirrorStrength * bounced;
                    } else {
                        radiance += uMirrorStrength * sky(rdir);
                    }
                }
                #endif

                if (uEnableAO == 1) {
                    float ao = computeAO(h, uFrameIndex);
                    radiance *= ao;
                }
            } else {
                // ==========================================================
                // ANALYTIC SCENE (plane + spheres)
                // ==========================================================
                MaterialProps mat = getMaterial(h.mat);

                if (mat.type == 2) {
                    // GLASS MATERIAL
                    // Use env-based glass shader; skip direct light / GI / AO
                    radiance = shadeGlass(h, V, mat, seed);
                } else {
                    // ---- Direct lighting (analytic)
                    radiance = directLight(h, seed, V);

                    // ---- One-bounce diffuse GI (analytic)
                    if (uEnableGI == 1 && mat.type == 0) {
                        // Only for diffuse/spec – skip mirrors / special mats
                        radiance += uGiScaleAnalytic * oneBounceGIAnalytic(h, uFrameIndex, seed);
                    }

                    #if ENABLE_MIRROR_BOUNCE
                    // Optional mirror reflection (analytic mirror sphere h.mat == 3)
                    if (uEnableMirror == 1 && h.mat == 3) {
                        vec3 rdir = reflect(dir, h.n);
                        vec3 rorg = h.p + rdir * uEPS;

                        Hit h2;
                        bool hit2 = traceAnalytic(rorg, rdir, h2);

                        if (hit2) {
                            vec3 V2 = -rdir;
                            vec3 bounced = directLight(h2, seed, V2);
                            radiance += uMirrorStrength * bounced;
                        } else {
                            radiance += uMirrorStrength * sky(rdir);
                        }
                    }
                    #endif

                    // ---- Ambient Occlusion modulation (subtle)
                    if (uEnableAO == 1) {
                        float ao = computeAO(h, uFrameIndex);
                        radiance *= ao;
                    }
                }
            }
        } else {
            // MISS → sky / environment
            radiance = sky(dir);

            // If the camera actually moved this frame, we want this pixel to
            // be treated as a disocclusion in TAA, to avoid "sphere→sky" smearing.
            if (uCameraMoved == 1 && s == 0) {
                motionOut = vec2(4.0, 4.0); // large NDC motion -> uvPrev OOB -> no history
                // GBuffer stays at default (0), which is fine for sky/background
            }
        }

        frameSum += radiance;
    }

    // Average current frame samples
    vec3 curr = frameSum / float(SPP);

    // Current UV
    vec2 uvCurr = vUV;

    // Effective motion for TAA:
    vec2 taaMotion = (uCameraMoved == 1) ? motionOut : vec2(0.0);

    vec4 taa = resolveTAA(curr, uvCurr, taaMotion, uPrevAccum, uFrameIndex);

    fragColor = taa;        // rgb = color, a = M2
    outMotion = motionOut;  // full motion kept for debug view (F6)
}
