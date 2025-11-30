#version 410 core

/*
    rt.frag – Ray/Path Tracing Entry Shader

    This fragment shader is the main entry point for the ray/path tracing pass.
    It is responsible for:

    - Generating primary rays from the camera (with optional per-frame jitter).
    - Tracing against either:
        * an analytic scene (plane + spheres), or
        * a BVH-accelerated triangle scene.
    - Evaluating direct lighting, one-bounce GI, AO, and environment lighting.
    - Writing multiple render targets (MRT):
        * COLOR0: accumulated linear color + M2 (for TAA/SVGF)
        * COLOR1: NDC motion (currentNDC - prevNDC) for TAA
        * COLOR2: world-space position (xyz)
        * COLOR3: world-space normal (xyz)
    - Handling TAA resolve by blending the current frame with history from
      the previous accumulation texture.

    This shader is intentionally modular and relies on several included files
    for scene description, BVH traversal, materials, lighting, and TAA logic.
*/

in vec2 vUV;

// COLOR0: accumulated linear color + M2
layout (location = 0) out vec4 fragColor;

// COLOR1: NDC motion (currentNDC - prevNDC)
layout (location = 1) out vec2 outMotion;

// COLOR2: world-space position (xyz, w unused)
layout (location = 2) out vec4 outGPos;

// COLOR3: world-space normal (xyz, w unused)
layout (location = 3) out vec4 outGNrm;

// Includes
#include "rt_uniforms.glsl"
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

    // Initialize outputs (will be refined by first hit or sky)
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
                    // DIFFUSE / PHONG MATERIALS

                    // Special case: point-light marker sphere – treat as emissive bulb
                    if (h.mat == MAT_POINTLIGHT_SPHERE) {
                        // Simple emissive model – bright, independent of other lights
                        vec3 baseCol = uPointLightColor * uPointLightIntensity;

                        // Optional: very mild falloff based on distance to camera,
                        // so it doesn't look insane when extremely close.
                        float d = length(h.p - uCamPos);
                        float falloff = 1.0 / max(d * d * 0.25 + 1.0, 1.0);

                        radiance = baseCol * falloff;

                        // No GI/AO on the emitter – it's self-lit.
                    } else {
                        // Regular diffuse objects (floor, main spheres)
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