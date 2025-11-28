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
uniform int uUseBVH;      // 0 = analytic (plane+spheres), 1 = BVH triangle scene
uniform int uNodeCount;
uniform int uTriCount;

// ---- BVH TBOs (only used if uUseBVH==1)
uniform samplerBuffer uBvhNodes;
uniform samplerBuffer uBvhTris;

// ---- Motion / reprojection
uniform int uShowMotion;    // 0 = normal, 1 = visualize motion (used in present)
uniform mat4 uPrevViewProj;
uniform mat4 uCurrViewProj;
uniform int uCameraMoved;   // 0 = camera is static, 1 = camera is moving

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
void main()
{
    // --------------------------------------------------------------------
    // Per-pixel setup (same for all SPP samples this frame)
    // --------------------------------------------------------------------
    int SPP = max(uSpp, 1);

    // Per-frame camera jitter (same for all SPP samples)
    vec2 camJit = (uEnableJitter == 1) ? uJitter : vec2(0.0);

    // Pixel UV / NDC and primary ray direction
    vec2 uv = (gl_FragCoord.xy + camJit) / uResolution;
    vec2 ndc = uv * 2.0 - 1.0;

    vec3 dir = normalize(
        uCamFwd
        + ndc.x * uCamRight * (uTanHalfFov * uAspect)
        + ndc.y * uCamUp * uTanHalfFov
    );

    // Initialize outputs
    vec3 frameSum = vec3(0.0);
    vec2 motionOut = vec2(0.0);
    outGPos = vec4(0.0);
    outGNrm = vec4(0.0);

    // --------------------------------------------------------------------
    // Path loop (per-sample shading, same primary ray; RNG changes per SPP)
    // --------------------------------------------------------------------
    for (int s = 0; s < SPP; ++s) {
        int seed = uFrameIndex * SPP + s;

        // Choose scene
        Hit h;
        bool hitAny = (uUseBVH == 1)
        ? traceBVH(uCamPos, dir, h)
        : traceAnalytic(uCamPos, dir, h);

        vec3 radiance;

        if (hitAny) {
            // ------------------------------------------------------------
            // First sample: write motion + GBuffer once to keep them stable
            // ------------------------------------------------------------
            if (s == 0) {
                vec2 prevNDC = ndcFromWorld(h.p, uPrevViewProj);
                vec2 currNDC = ndcFromWorld(h.p, uCurrViewProj);
                motionOut = currNDC - prevNDC;

                outGPos = vec4(h.p, 1.0);
                outGNrm = vec4(normalize(h.n), 0.0);
            }

            vec3 V = -dir; // direction from hit → camera

            if (uUseBVH == 1) {
                // =======================================================
                // BVH SCENE (triangles)
                // =======================================================
                radiance = directLightBVH(h, seed, V);

                if (uEnableGI == 1) {
                    radiance += uGiScaleBVH * oneBounceGIBVH(h, uFrameIndex, seed);
                }

                if (uEnableAO == 1) {
                    float ao = computeAO(h, uFrameIndex);
                    radiance *= ao;
                }
            } else {
                // =======================================================
                // ANALYTIC SCENE (plane + spheres)
                // =======================================================
                MaterialProps mat = getMaterial(h.mat);

                if (mat.type == 2) {
                    // GLASS MATERIAL
                    radiance = shadeGlass(h, V, mat, seed);

                } else if (mat.type == 1) {
                    // MIRROR MATERIAL
                    radiance = shadeMirror(h, V, mat, seed);

                } else {
                    // DIFFUSE / PHONG MATERIAL
                    radiance = directLight(h, seed, V);

                    if (uEnableGI == 1) {
                        radiance += uGiScaleAnalytic * oneBounceGIAnalytic(h, uFrameIndex, seed);
                    }

                    if (uEnableAO == 1) {
                        float ao = computeAO(h, uFrameIndex);
                        radiance *= ao;
                    }
                }
            }
        } else {
            // ------------------------------------------------------------
            // MISS → sky / environment
            // ------------------------------------------------------------
            radiance = sky(dir);

            // If the camera moved, mark this pixel as a disocclusion for TAA
            // so we don't smear "geometry → sky" transitions.
            if (uCameraMoved == 1 && s == 0) {
                motionOut = vec2(4.0, 4.0); // large NDC motion → uvPrev OOB → no history
                // GBuffer remains at default (0), which is fine for sky/background
            }
        }

        frameSum += radiance;
    }

    // --------------------------------------------------------------------
    // TAA resolve (curr frame average + motion → history blend)
    // --------------------------------------------------------------------
    vec3 curr = frameSum / float(SPP);
    vec2 uvCurr = vUV; // for sampling uPrevAccum

    // Effective motion for TAA: if camera is "static", treat as zero motion
    vec2 taaMotion = (uCameraMoved == 1) ? motionOut : vec2(0.0);

    vec4 taa = resolveTAA(curr, uvCurr, taaMotion, uPrevAccum, uFrameIndex);

    // COLOR0: final accumulated color + M2
    fragColor = taa;

    // COLOR1: motion stored separately for present-time debug visualization
    outMotion = motionOut;
}