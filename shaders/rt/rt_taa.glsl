// rt_taa.glsl
#ifndef RT_TAA_GLSL
#define RT_TAA_GLSL

// Curr: current frame color (already averaged over SPP)
// uvCurr: current pixel UV
// motionOut: NDC motion (currNDC - prevNDC)
// prevAccum: history texture (RGB = color, A = second moment of luma)
// frameIndex: 0,1,2,... (from Accum)
vec4 resolveTAA(vec3      curr,
                vec2      uvCurr,
                vec2      motionOut,
                sampler2D prevAccum,
                int       frameIndex)
{
    // Luma coefficients (approx. Rec.709)
    const vec3 YCOEFF = vec3(0.299, 0.587, 0.114);

    float lCurr = dot(curr, YCOEFF);
    float lCurr2 = lCurr * lCurr;

    // ---------------------------------------------
    // First frame: no valid history yet
    // ---------------------------------------------
    if (frameIndex == 0) {
        // Start second moment as lCurr^2, variance will be 0 anyway
        return vec4(curr, lCurr2);
    }

    // Motion magnitude in NDC
    float motMag = length(motionOut);
    const float STILL_THRESH = 1e-4;

    // ---------------------------------------------
    // CASE 1: camera/pixel effectively still
    //   → true running average (best convergence)
    // ---------------------------------------------
    if (motMag < STILL_THRESH) {
        vec4 prevRGBA = texture(prevAccum, uvCurr);
        vec3 prevCol  = prevRGBA.rgb;
        float prevM2  = prevRGBA.a;   // previous second moment of luma

        float n = float(frameIndex);

        // New running mean in RGB
        vec3 meanNew = (prevCol * n + curr) / (n + 1.0);

        // Running second moment of luma
        float m2New = (prevM2 * n + lCurr2) / (n + 1.0);

        // Variance in luma: Var[x] = E[x^2] - (E[x])^2
        float lMeanNew = dot(meanNew, YCOEFF);
        float varNew   = max(m2New - lMeanNew * lMeanNew, 0.0);

        return vec4(meanNew, varNew);
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

    // Fetch previous color + stored second moment (in A).
    // For disocclusions, reset history to current sample.
    vec4 prevRGBA = oob
    ? vec4(curr, lCurr2)             // start fresh
    : texture(prevAccum, uvPrev);

    vec3 prevCol = prevRGBA.rgb;
    float prevM2 = prevRGBA.a;

    // ------------------------------------------------------------------
    // History confidence:
    //   - motion-based (fast pixels => less history)
    //   - color-delta-based (large color change => less history)
    // ------------------------------------------------------------------

    float n = float(frameIndex);
    float wHist;

    if (oob) {
        wHist = 0.0;   // disocclusion: trust current
        n     = 0.0;   // reset moment statistics
    } else {
        // 1.0 when motMag ≈ 0, fades toward 0 when motMag grows
        wHist = 1.0 - smoothstep(0.02, 0.25, motMag);
        wHist = clamp(wHist, 0.0, 0.96);
    }

    // Color-based confidence using luma (approx. Rec.709)
    float lPrev   = dot(prevCol, YCOEFF);
    float maxL    = max(max(lCurr, lPrev), 1e-3);
    float relDiff = abs(lCurr - lPrev) / maxL;  // 0 = same, >0 = different

    // Small relDiff → keep history, large relDiff → shrink history
    float colorWeight = 1.0 - smoothstep(0.05, 0.3, relDiff);
    wHist *= colorWeight;

    // Clamp so history never fully dominates
    wHist = clamp(wHist, 0.0, 0.96);
    float wCurr = 1.0 - wHist;

    // --- History clamping box around current ---
    vec3 historyCol = prevCol;
    float box = 0.12; // tweak: 0.08–0.15 usually works well
    vec3 lo = curr - vec3(box);
    vec3 hi = curr + vec3(box);
    historyCol = clamp(historyCol, lo, hi);

    // Final TAA blended color
    vec3 taaCol = wHist * historyCol + wCurr * curr;

    // ------------------------------------------------------------------
    // Temporal variance estimate (SVGF-style 1D moment)
    // ------------------------------------------------------------------
    // We maintain a running second moment of luma (M2) in A:
    //   M2_new = (M2_prev * n + lCurr^2) / (n + 1)
    // Then variance:
    //   Var = M2_new - (E[l])^2, where E[l] ≈ luma of taaCol.
    float m2New    = (prevM2 * n + lCurr2) / (n + 1.0);
    float lMeanNew = dot(taaCol, YCOEFF);
    float varNew   = max(m2New - lMeanNew * lMeanNew, 0.0);

    return vec4(taaCol, varNew);
}

#endif // RT_TAA_GLSL