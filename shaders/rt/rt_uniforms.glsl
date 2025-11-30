// rt_uniforms.glsl
#ifndef RT_UNIFORMS_GLSL
#define RT_UNIFORMS_GLSL

/*
    rt_uniforms.glsl – Shared uniform block for ray tracing & present passes

    This file centralizes all GLSL uniforms used by the ray tracing pipeline
    (primary ray generation, analytic scene, BVH traversal, GI/AO, materials,
    environment lighting, and TAA/SVGF).

    The idea is to keep CPU ↔ GPU bindings in one place so:
      - The C++ side (RenderParams + AppState) has a single source of truth
        for parameter names and semantics.
      - All the GLSL helper modules (rt_common, rt_lighting, rt_scene_analytic,
        rt_bvh, rt_taa, etc.) can include this and agree on the same layout.

    Most of these uniforms are directly backed by RenderParams on the C++ side
    and are updated every frame via the UI / ImGui controls.
*/

// ------------------------------------------------------------
// Global numeric constants (from RenderParams)
// ------------------------------------------------------------
uniform float uEPS;   // Epsilon for ray offsets / intersection robustness
uniform float uPI;    // Pi constant (for any angular math)
uniform float uINF;   // Large sentinel "infinity" distance

// ------------------------------------------------------------
// Camera & primary ray generation / accumulation
// ------------------------------------------------------------
// Camera basis in world space (right-handed)
uniform vec3 uCamPos;    // Camera origin in world space
uniform vec3 uCamRight;  // Camera right vector
uniform vec3 uCamUp;     // Camera up vector
uniform vec3 uCamFwd;    // Camera forward vector

// Projection parameters
uniform float uTanHalfFov; // tan(fovY * 0.5)
uniform float uAspect;     // viewport aspect ratio (width / height)

// Accumulation state
uniform int uFrameIndex; // Current frame index (for jitter / history)
uniform int uSpp;        // Samples per pixel accumulated so far

// Render target resolution
uniform vec2 uResolution; // (width, height) in pixels

// Previous accumulation buffer (for TAA / temporal reuse)
//  - RGB = accumulated linear color
//  - A   = second moment of luma (M2) for variance estimation
uniform sampler2D uPrevAccum;

// ------------------------------------------------------------
// Jitter (for TAA / stochastic sampling)
// ------------------------------------------------------------
uniform vec2 uJitter;        // Subpixel camera jitter (NDC or pixel-based)
uniform int uEnableJitter;   // 0 = off, 1 = on

// ------------------------------------------------------------
// Scene mode & BVH configuration
// ------------------------------------------------------------
// Scene mode selector:
//   0 = analytic scene (planes + spheres)
//   1 = BVH triangle scene
uniform int uUseBVH;

// BVH statistics (for debug / sanity)
uniform int uNodeCount;     // Number of BVH nodes
uniform int uTriCount;      // Number of triangles in BVH scene

// BVH data, bound as texture buffers (used when uUseBVH == 1)
uniform samplerBuffer uBvhNodes; // Packed BVH nodes
uniform samplerBuffer uBvhTris;  // Packed triangle data

// ------------------------------------------------------------
// Motion vectors & reprojection (for TAA / motion debug)
// ------------------------------------------------------------
// Motion visualization toggle:
//   0 = normal shading
//   1 = visualize motion vectors (used in present pass)
uniform int uShowMotion;

// View-projection matrices for motion reprojection
uniform mat4 uPrevViewProj; // Previous frame view-projection
uniform mat4 uCurrViewProj; // Current frame view-projection

// Camera movement flag:
//   0 = camera considered static
//   1 = camera moved (used to clamp/discard history)
uniform int uCameraMoved;

// ------------------------------------------------------------
// TAA parameters (from RenderParams)
// ------------------------------------------------------------
// Threshold below which history is considered "still"
uniform float uTaaStillThresh;

// Threshold above which pixels are treated as "hard moving"
uniform float uTaaHardMovingThresh;

// History blend weights for min/avg/max cases
uniform float uTaaHistoryMinWeight;
uniform float uTaaHistoryAvgWeight;
uniform float uTaaHistoryMaxWeight;

// Neighborhood clamping box size (in color space units)
uniform float uTaaHistoryBoxSize;

// TAA toggle:
//   0 = disabled
//   1 = enabled
uniform int uEnableTAA;

// ------------------------------------------------------------
// GI / AO parameters (from RenderParams)
// ------------------------------------------------------------
// Global illumination scale factors
uniform float uGiScaleAnalytic; // GI scale for analytic scene
uniform float uGiScaleBVH;      // GI scale for BVH scene

// GI/AO feature toggles
uniform int uEnableGI;        // 0 = off, 1 = on
uniform int uEnableAO;        // 0 = off, 1 = on

// Ambient occlusion sampling parameters
uniform int uAO_SAMPLES;      // Number of AO samples
uniform float uAO_RADIUS;     // AO sampling radius
uniform float uAO_BIAS;       // AO normal bias
uniform float uAO_MIN;        // Minimum AO factor (floor)

// ------------------------------------------------------------
// Environment map (cubemap-based lighting)
// ------------------------------------------------------------
uniform samplerCube uEnvMap;   // Environment cubemap
uniform int uUseEnvMap;        // 0 = disabled, 1 = enabled
uniform float uEnvIntensity;   // Global envmap intensity multiplier

// ------------------------------------------------------------
// Hybrid lighting: Sun + Sky + Point
// ------------------------------------------------------------
// Directional "sun" light
uniform int uSunEnabled;       // 0 = off, 1 = on
uniform vec3 uSunColor;        // Sun color (RGB)
uniform float uSunIntensity;   // Sun intensity (scalar)
uniform vec3 uSunDir;          // Direction FROM light TO scene (sun rays = -uSunDir)

// Simple hemispherical sky light
uniform int uSkyEnabled;       // 0 = off, 1 = on
uniform vec3 uSkyColor;        // Base sky color
uniform float uSkyIntensity;   // Sky intensity multiplier
uniform vec3 uSkyUpDir;        // "Up" direction for sky dome

// Local point light (also has a small marker sphere in the scene)
uniform int uPointLightEnabled;    // 0 = off, 1 = on
uniform vec3 uPointLightPos;       // World-space position of the point light
uniform vec3 uPointLightColor;     // Light color (RGB)
uniform float uPointLightIntensity;// Light intensity (scalar)

// ------------------------------------------------------------
// Material parameters (GUI-controlled)
// ------------------------------------------------------------
// Left albedo sphere (diffuse / glossy)
uniform vec3 uMatAlbedo_AlbedoColor;   // Base albedo color
uniform float uMatAlbedo_SpecStrength;  // Specular strength
uniform float uMatAlbedo_Gloss;         // Glossiness / shininess

// Glass sphere (refraction / distortion)
uniform vec3 uMatGlass_Albedo;      // Tint color of glass
uniform float uMatGlass_IOR;        // Index of refraction
uniform float uMatGlass_Distortion; // Distortion / roughness factor
uniform int uMatGlass_Enabled;      // 0 = behave like diffuse, 1 = glass

// Mirror sphere (perfect / glossy mirror)
uniform vec3 uMatMirror_Albedo;     // Mirror tint
uniform float uMatMirror_Gloss;     // Glossiness; higher = sharper reflection
uniform int uMatMirror_Enabled;     // 0 = fallback to diffuse, 1 = mirror

#endif // RT_UNIFORMS_GLSL