#pragma once

#include <cstdint>

struct RenderParams {
    // core
    int sppPerFrame = 1;
    float exposure = 1.0f;

    // Materials
    int enableMirror = 1;
    float mirrorStrength = 0.9f;

    // Jitter
    int enableJitter = 1;
    float jitterStillScale = 0.25f;
    float jitterMovingScale = 0.5f;

    // GI
    int enableGI = 1;
    float giScaleAnalytic = 0.35f;
    float giScaleBVH = 0.20f;

    // AO
    int enableAO = 1;
    int aoSamples = 4;
    float aoRadius = 0.8;
    float aoBias = 2e-3;
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
    const float EPS = 1e-4;
    const float PI = 3.1415926535;
    const float INF = 1e30;

    // Debug
    float motionScale = 4.0f;
};

struct RenderParamsUBO {
    std::uint32_t ubo = 0;

    void create();
    void destroy();
    void upload(const RenderParams &p);
};
