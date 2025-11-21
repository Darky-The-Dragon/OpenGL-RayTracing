#ifndef RT_TAA_GLSL
#define RT_TAA_GLSL

// Curr: current frame color (already averaged over SPP)
// uvCurr: current pixel UV
// motionOut: NDC motion (currNDC - prevNDC) -- *after* camera-moved gating
// prevAccum: history texture (RGB = color, A = second moment of luma M2)
// frameIndex: 0,1,2,... (from Accum)
vec4 resolveTAA(vec3 curr,
                vec2 uvCurr,
                vec2 motionOut,
                sampler2D prevAccum,
                int frameIndex)
{
    // Luma coefficients (approx. Rec.709)
    const vec3 YCOEFF = vec3(0.299, 0.587, 0.114);

    float lCurr = dot(curr, YCOEFF);
    float lCurr2 = lCurr * lCurr;

    // ---------------------------------------------
    // First frame: no valid history yet
    // ---------------------------------------------
    if (frameIndex == 0) {
        // Store color + second moment of luma (M2 = E[l^2])
        return vec4(curr, lCurr2);
    }

    // Motion magnitude in NDC
    float motMag = length(motionOut);

    // Use CPU-driven params instead of hard-coded constants
    float MIN_W_HIST = uTaaHistoryMinWeight;
    float AVG_W_HIST = uTaaHistoryAvgWeight;
    float MAX_W_HIST = uTaaHistoryMaxWeight;
    float BOX_SIZE = uTaaHistoryBoxSize;

    // ---------------------------------------------
    // CASE 1: camera/pixel effectively still
    //   → exponential running average (fast convergence)
    // ---------------------------------------------
    if (motMag < uTaaStillThresh) {
        vec4 prevRGBA = texture(prevAccum, uvCurr);
        vec3 prevCol = prevRGBA.rgb;
        float prevM2 = prevRGBA.a;

        // More aggressive accumulation when camera is still.
        // Early frames converge faster, later frames become VERY stable.
        float wHist;
        if (frameIndex < 8) {
            wHist = MIN_W_HIST;  // warm-up
        } else if (frameIndex < 32) {
            wHist = AVG_W_HIST;
        } else {
            wHist = MAX_W_HIST;  // e.g. 0.98 from RenderParams
        }
        float wCurr = 1.0 - wHist;

        vec3 meanNew = prevCol * wHist + curr * wCurr;
        float m2New = prevM2 * wHist + lCurr2 * wCurr;

        return vec4(meanNew, m2New);
    }

    // ---------------------------------------------
    // CASE 2: pixel is moving → motion-vector TAA
    // ---------------------------------------------

    // Reproject into previous frame using motion (NDC → UV)
    vec2 uvPrev = uvCurr - motionOut * 0.5;

    // Out-of-bounds => disocclusion: no reliable history
    bool oob =
    any(lessThan(uvPrev, vec2(0.0))) ||
    any(greaterThan(uvPrev, vec2(1.0)));

    if (oob) {
        return vec4(curr, lCurr2);
    }

    // Fetch previous color + stored second moment (M2) in A.
    vec4 prevRGBA = texture(prevAccum, uvPrev);
    vec3 prevCol = prevRGBA.rgb;
    float prevM2 = prevRGBA.a;

    // ------------------------------------------------------------------
    // History confidence:
    //   - motion-based (fast pixels => less history)
    //   - color-delta-based (large color change => less history)
    //   - extra disocclusion guard for sky/geometry edges
    // ------------------------------------------------------------------

    // Base: 1.0 when motMag ≈ 0, fades toward 0 as motMag grows
    float wHist = 1.0 - smoothstep(0.02, uTaaHardMovingThresh, motMag);

    // If motion is really large, just kill history completely
    if (motMag > uTaaHardMovingThresh) {
        wHist = 0.0;
    }

    // Color-based confidence using luma
    float lPrev = dot(prevCol, YCOEFF);
    float maxL = max(max(lCurr, lPrev), 1e-3);
    float relDiff = abs(lCurr - lPrev) / maxL;  // 0 = same, >0 = different

    // Small relDiff → keep history, large relDiff → shrink history
    float colorWeight = 1.0 - smoothstep(0.03, 0.25, relDiff);
    wHist *= colorWeight;

    // --- Extra kill-switch for strong color changes (sky/geometry edges) ---
    bool bigColorChange =
    (motMag > 0.02) &&
    (relDiff > 0.30);   // sensitive enough to kill plane/sky streaks

    if (bigColorChange) {
        wHist = 0.0;
    }

    // Clamp so history never fully dominates (uses param instead of literal 0.90)
    wHist = clamp(wHist, 0.0, MAX_W_HIST);
    float wCurr = 1.0 - wHist;

    // --- History clamping box around current (param-driven) ---
    vec3 historyCol = prevCol;
    vec3 lo = curr - vec3(BOX_SIZE);
    vec3 hi = curr + vec3(BOX_SIZE);
    historyCol = clamp(historyCol, lo, hi);

    // Final TAA blended color
    vec3 taaCol = wHist * historyCol + wCurr * curr;

    // ------------------------------------------------------------------
    // Temporal second moment estimate (SVGF-style, 1D moment on luma)
    // ------------------------------------------------------------------
    float m2New = wHist * prevM2 + wCurr * lCurr2;

    return vec4(taaCol, m2New);
}

#endif // RT_TAA_GLSL