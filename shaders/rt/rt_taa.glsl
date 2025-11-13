// rt_taa.glsl
#ifndef RT_TAA_GLSL
#define RT_TAA_GLSL

// TAA resolve using motion vectors + simple disocclusion & clamp
vec3 resolveTAA(vec3 curr, vec2 uvCurr, vec2 motionOut,
                sampler2D prevAccum, int frameIndex) {

    // --- First frame: no history yet, just output current ---
    if (frameIndex == 0) {
        return curr;
    }

    // Motion magnitude in NDC (how fast this pixel is moving)
    float motMag = length(motionOut);

    // === CASE 1: Camera effectively still → use true running average ===
    const float STILL_THRESH = 1e-4;
    if (motMag < STILL_THRESH) {
        vec3 prev = texture(prevAccum, uvCurr).rgb;
        float n   = float(frameIndex);
        return (prev * n + curr) / (n + 1.0);
    }

    // === CASE 2: Pixel is moving → use motion-vector TAA ===

    // Reproject into previous frame using motionOut (NDC -> UV)
    vec2 uvPrev = uvCurr - motionOut * 0.5;

    // Out-of-bounds => disocclusion: trust current
    bool oob =
    any(lessThan(uvPrev, vec2(0.0))) ||
    any(greaterThan(uvPrev, vec2(1.0)));

    vec3 history = oob ? curr : texture(prevAccum, uvPrev).rgb;

    // Base history weight:
    float wHist;
    if (oob) {
        wHist = 0.0;                     // disocclusion: no history
    } else {
        // 1.0 when motMag ≈ 0, fades toward 0 when motMag grows
        wHist = 1.0 - smoothstep(0.02, 0.25, motMag);
        // Clamp so history never fully dominates
        wHist = clamp(wHist, 0.0, 0.96);
    }
    float wCurr = 1.0 - wHist;

    // --- History clamping box around current ---
    float box = 0.12; // tweak: 0.08–0.15 usually works well
    vec3 lo = curr - vec3(box);
    vec3 hi = curr + vec3(box);
    history = clamp(history, lo, hi);

    // Final TAA color
    return wHist * history + wCurr * curr;
}

#endif // RT_TAA_GLSL