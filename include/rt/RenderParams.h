struct RenderParams {
    // core
    int   sppPerFrame    = 1;
    float exposure       = 1.0f;

    // GI
    float giScaleAnalytic = 0.35f;
    float giScaleBVH      = 0.20f;
    int   enableGI        = 1;
    int   enableAO        = 1;
    int   enableMirror    = 1;

    // TAA
    float taaStillThresh       = 1e-5f;
    float taaHardMovingThresh  = 0.35f;
    float taaHistoryMaxWeight  = 0.96f;
    float taaHistoryBoxSize    = 0.06f;

    // SVGF
    float svgfVarMax   = 0.02f;
    float svgfKVar     = 200.0f;
    float svgfKColor   = 20.0f;
    float svgfStrength = 0.6f;   // 0 = no spatial denoise, 1 = full SVGF
    // start around 0.5–0.7

    // Debug
    float motionScale = 4.0f;
};