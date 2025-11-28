#pragma once

#include <cstdint>

struct RenderParams {
    // core
    int sppPerFrame = 1;
    float exposure = 1.0f;

    // --- Material Controls ---
    float matAlbedoColor[3] = {0.85f, 0.25f, 0.25f};
    float matAlbedoSpecStrength = 0.35f;
    float matAlbedoGloss = 48.0f;

    int matGlassEnabled = 1;
    float matGlassColor[3] = {0.95f, 0.98f, 1.0f};
    float matGlassIOR = 1.5f;
    float matGlassDistortion = 0.05f;

    int matMirrorEnabled = 1;
    float matMirrorColor[3] = {1.0f, 1.0f, 1.0f};
    float matMirrorGloss = 256.0f;

    // Jitter
    int enableJitter = 1;
    float jitterStillScale = 0.25f;
    float jitterMovingScale = 0.5f;

    // GI
    int enableGI = 1;
    float giScaleAnalytic = 0.35f;
    float giScaleBVH = 0.20f;

    // CubeMap
    int enableEnvMap = 1;
    float envMapIntensity = 1.0f;

    // --- Lights (Hybrid: Sun + Sky + Point) ---

    // Sun (directional light)
    int sunEnabled = 1;
    float sunColor[3] = {1.0f, 0.95f, 0.85f};
    float sunIntensity = 0.45f; // key light strength
    float sunYaw = 45.0f; // degrees
    float sunPitch = -35.0f; // degrees (negative = from above)

    // Sky (ambient-ish dome, AO will handle occlusion feel)
    int skyEnabled = 1;
    float skyColor[3] = {0.4f, 0.5f, 1.0f};
    float skyIntensity = 1.0f;
    float skyYaw = 0.0f; // mostly for artistic tweak
    float skyPitch = 90.0f; // up direction

    // Point light above the plane
    int pointLightEnabled = 1;
    float pointLightColor[3] = {1.0f, 0.9f, 0.7f};
    float pointLightIntensity = 20.0f;

    // Base position (also acts as orbit center)
    float pointLightPos[3] = {0.0f, 2.5f, -3.0f};

    // Orbit controls (around Y axis)
    int pointLightOrbitEnabled = 0; // 0 = static, 1 = orbit
    float pointLightOrbitRadius = 3.5f; // orbit radius in XZ plane
    float pointLightOrbitSpeed = 0.02f; // radians per frame

    // AO
    int enableAO = 1;
    int aoSamples = 4;
    float aoRadius = 0.8f;
    float aoBias = 2e-3f;
    float aoMin = 0.5f;

    // TAA
    int enableTAA = 1;
    float taaStillThresh = 1e-5f;
    float taaHardMovingThresh = 0.35f;
    float taaHistoryMinWeight = 0.85f;
    float taaHistoryAvgWeight = 0.92f;
    float taaHistoryMaxWeight = 0.96f;
    float taaHistoryBoxSize = 0.06f;

    // SVGF
    int enableSVGF = 1;
    float svgfVarMax = 0.02f;
    float svgfKVar = 200.0f;
    float svgfKColor = 20.0f;
    float svgfKVarMotion = 35.0f;
    float svgfKColorMotion = 3.0f;
    float svgfStrength = 0.6f;
    float svgfVarEPS = 2e-4f;
    float svgfMotionEPS = 0.005f;

    // Constants
    static constexpr float EPS = 1e-4f;
    static constexpr float PI = 3.1415926535f;
    static constexpr float INF = 1e30f;

    // Debug
    float motionScale = 4.0f;
};
