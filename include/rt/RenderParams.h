#pragma once

struct RenderParams {
    // core
    int sppPerFrame = 1;
    float exposure = 1.0f;
    float jitterScale = 0.5f;

    // GI
    float giScaleAnalytic = 0.35f;
    float giScaleBVH = 0.20f;
    int enableGI = 1;
    int enableAO = 1;
    int enableMirror = 1;
    float mirrorStrength = 0.9f;

    // AO
    int aoSamples = 4;
    float aoRadius = 0.8;
    float aoBias = 2e-3;
    float aoMin = 0.5f;

    // TAA
    float taaStillThresh = 1e-5f;
    float taaHardMovingThresh = 0.35f;
    float taaHistoryMinWeight = 0.85f;
    float taaHistoryAvgWeight = 0.92f;
    float taaHistoryMaxWeight = 0.96f;
    float taaHistoryBoxSize = 0.06f;

    // SVGF
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
    const float PI  = 3.1415926535;
    const float INF = 1e30;

    // Debug
    float motionScale = 4.0f;
};
