#pragma once

/**
 * @struct RenderParams
 * @brief Collection of all user-tunable rendering parameters.
 *
 * This struct centralizes the entire set of parameters exposed to the UI.
 * It includes lighting, materials, GI, jitter, TAA, SVGF, and debug controls.
 * The renderer reads these values every frame when updating shader uniforms.
 *
 * Keeping all tunables in one structure avoids scattered configuration and
 * improves reproducibility of experiments or comparisons for the project.
 */
struct RenderParams {
    // -------------------------------------------------------------------------
    // Core render settings
    // -------------------------------------------------------------------------

    /// Samples per pixel accumulated per frame (1, 2, 4, 8, 16).
    int sppPerFrame = 1;

    /// Exposure multiplier used in tone mapping.
    float exposure = 1.0f;

    // -------------------------------------------------------------------------
    // Material controls
    // -------------------------------------------------------------------------

    /// Base albedo color for matte/diffuse materials.
    float matAlbedoColor[3] = {0.85f, 0.25f, 0.25f};

    /// Specular reflection strength for the albedo material.
    float matAlbedoSpecStrength = 0.35f;

    /// Glossiness exponent controlling highlight sharpness.
    float matAlbedoGloss = 48.0f;

    /// Enable/disable glass material.
    int matGlassEnabled = 1;

    /// Base tint applied to glass transmission.
    float matGlassColor[3] = {0.95f, 0.98f, 1.0f};

    /// Index of refraction for glass (e.g., 1.5 for typical glass).
    float matGlassIOR = 1.5f;

    /// Small distortion factor used to simulate micro-imperfections.
    float matGlassDistortion = 0.05f;

    /// Enable/disable mirror material.
    int matMirrorEnabled = 1;

    /// Mirror reflectance color.
    float matMirrorColor[3] = {1.0f, 1.0f, 1.0f};

    /// Glossiness exponent for mirror reflections.
    float matMirrorGloss = 256.0f;

    // -------------------------------------------------------------------------
    // Jitter / Anti-Aliasing
    // -------------------------------------------------------------------------

    /// Enables per-pixel jitter for stochastic sampling.
    int enableJitter = 1;

    /// Jitter scale when camera is still.
    float jitterStillScale = 0.25f;

    /// Jitter scale when camera is moving.
    float jitterMovingScale = 0.5f;

    // -------------------------------------------------------------------------
    // Global Illumination
    // -------------------------------------------------------------------------

    /// Enables global illumination contributions.
    int enableGI = 1;

    /// Strength of analytic GI terms.
    float giScaleAnalytic = 0.35f;

    /// Strength of BVH-based GI terms.
    float giScaleBVH = 0.20f;

    // -------------------------------------------------------------------------
    // Environment Map
    // -------------------------------------------------------------------------

    /// Enables IBL via environment map.
    int enableEnvMap = 1;

    /// Intensity multiplier for the environment lighting.
    float envMapIntensity = 1.0f;

    // -------------------------------------------------------------------------
    // Lighting (Directional Sun + Sky Dome + Optional Point Light)
    // -------------------------------------------------------------------------

    /// Enable directional sunlight.
    int sunEnabled = 1;

    /// Sunlight color.
    float sunColor[3] = {1.0f, 0.95f, 0.85f};

    /// Direct sun intensity.
    float sunIntensity = 0.45f;

    /// Horizontal angle of the sun (degrees).
    float sunYaw = 45.0f;

    /// Vertical angle of the sun (degrees; negative = above).
    float sunPitch = -35.0f;

    /// Enable sky dome ambient.
    int skyEnabled = 1;

    /// Sky ambient color.
    float skyColor[3] = {0.4f, 0.5f, 1.0f};

    /// Sky intensity multiplier.
    float skyIntensity = 1.0f;

    /// Horizontal rotation of the sky dome.
    float skyYaw = 0.0f;

    /// Vertical rotation of the sky dome (typically around axis-up).
    float skyPitch = 90.0f;

    /// Enable point light.
    int pointLightEnabled = 1;

    /// Color of the point light.
    float pointLightColor[3] = {1.0f, 0.9f, 0.7f};

    /// Strength of the point light (in arbitrary units).
    float pointLightIntensity = 20.0f;

    /// Base world position of the point light.
    float pointLightPos[3] = {0.0f, 2.5f, -3.0f};

    /// Whether the point light is orbiting around the Y axis.
    int pointLightOrbitEnabled = 0;

    /// Radius of the orbit (XZ plane).
    float pointLightOrbitRadius = 3.5f;

    /// Angular speed of the point light orbit (degrees per second).
    float pointLightOrbitSpeed = 20.0f;

    /// Explicit yaw rotation of the point light (degrees).
    float pointLightYaw = 0.0f;

    /// Explicit pitch rotation of the point light (degrees).
    float pointLightPitch = 0.0f;

    // -------------------------------------------------------------------------
    // Ambient Occlusion
    // -------------------------------------------------------------------------

    /// Enable ambient occlusion.
    int enableAO = 1;

    /// Number of AO samples per pixel.
    int aoSamples = 4;

    /// AO sampling radius in world units.
    float aoRadius = 0.8f;

    /// Small bias to avoid self-intersection artifacts.
    float aoBias = 2e-3f;

    /// Minimum ambient light contribution.
    float aoMin = 0.5f;

    // -------------------------------------------------------------------------
    // TAA (Temporal Anti-Aliasing)
    // -------------------------------------------------------------------------

    /// Enables TAA filtering.
    int enableTAA = 1;

    /// Threshold for detecting still fragments (lower = more stable history).
    float taaStillThresh = 1e-5f;

    /// Threshold for detecting hard motion, flushing history aggressively.
    float taaHardMovingThresh = 0.35f;

    /// Minimum history blending weight.
    float taaHistoryMinWeight = 0.85f;

    /// Average history blending weight.
    float taaHistoryAvgWeight = 0.92f;

    /// Maximum allowable history weight.
    float taaHistoryMaxWeight = 0.96f;

    /// Spatial neighborhood used for history clamping.
    float taaHistoryBoxSize = 0.06f;

    // -------------------------------------------------------------------------
    // SVGF Denoiser
    // -------------------------------------------------------------------------

    /// Enables the SVGF pipeline.
    int enableSVGF = 1;

    /// Maximum variance clamp.
    float svgfVarMax = 0.02f;

    /// Variance kernel constant for normal scenes.
    float svgfKVar = 200.0f;

    /// Color kernel constant for normal scenes.
    float svgfKColor = 20.0f;

    /// Variance kernel constant when motion is detected.
    float svgfKVarMotion = 35.0f;

    /// Color kernel constant when motion is detected.
    float svgfKColorMotion = 3.0f;

    /// Final SVGF blending strength.
    float svgfStrength = 0.6f;

    /// Small epsilon to avoid division instability in variance.
    float svgfVarEPS = 2e-4f;

    /// Small epsilon for motion confidence checks.
    float svgfMotionEPS = 0.005f;

    // -------------------------------------------------------------------------
    // Fundamental constants
    // -------------------------------------------------------------------------

    static constexpr float EPS = 1e-4f; ///< Small epsilon constant.
    static constexpr float PI = 3.1415926535f; ///< π constant.
    static constexpr float INF = 1e30f; ///< Large sentinel value.

    // -------------------------------------------------------------------------
    // Debug Controls
    // -------------------------------------------------------------------------

    /// Scales the visualization of motion vectors.
    float motionScale = 4.0f;
};
