#version 410 core

/*
    rt_present.frag – TAA + SVGF Present / Post-Processing Shader

    This shader:
    - Reads the history buffer (uTex), which stores:
        * rgb = accumulated linear color
        * a   = M2 (second moment of luma) used to estimate variance.
    - Uses the motion buffer (uMotionTex) to:
        * Visualize motion (debug mode), or
        * Drive a variance- and motion-aware SVGF-lite spatial filter.
    - Optionally applies a GBuffer-aware spatial filter (svgfFilter):
        * Uses world-space position (uGPos) and normal (uGNrm) to stop blurring
          across geometry edges.
    - Applies ACES tonemapping and gamma correction to output sRGB.

    Controls:
    - uShowMotion: switches between normal path and motion visualization.
    - uEnableSVGF: toggles the SVGF-lite filter.
    - uSvgfStrength: blends between raw TAA output and filtered result.
    - uKVar/uKColor, uKVarMotion/uKColorMotion: tuning parameters for the filter.

    The shader is intended as a lightweight, real-time friendly post-process
    that builds on top of the ray-traced accumulation + TAA pass.
*/

in vec2 vUV;                    // kept for compatibility, not used for sampling
out vec4 fragColor;

uniform sampler2D uTex;        // history buffer: rgb = color, a = M2 (second moment of luma)
uniform sampler2D uMotionTex;  // RG16F, NDC motion (currNDC - prevNDC)

// GBuffer samplers
uniform sampler2D uGPos;       // world-space position
uniform sampler2D uGNrm;       // world-space normal

uniform float uExposure;
uniform int uShowMotion;       // 0 = normal, 1 = visualize motion
uniform float uMotionScale;    // e.g. 4.0
uniform vec2 uResolution;      // framebuffer size in pixels

// SVGF params / controls
uniform float uVarMax;
uniform float uKVar;
uniform float uKColor;
uniform float uKVarMotion;
uniform float uKColorMotion;
uniform float uSvgfStrength;
uniform int uEnableSVGF;

// Luma coefficients (approx. Rec.709)
const vec3 YCOEFF = vec3(0.299, 0.587, 0.114);

// -----------------------------------------------------------------------------
// Tonemapping and color utilities
// -----------------------------------------------------------------------------

/**
 * @brief ACES approximation tonemapper (Narkowicz 2015).
 *
 * Applies exposure, then maps HDR linear color into [0,1] with a curve
 * that maintains highlight rolloff and overall contrast.
 */
vec3 acesTonemap(vec3 x) {
    x *= uExposure;
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

/**
 * @brief Converts HSV to RGB, mainly used for motion visualization.
 */
vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

// ------------------------------------------------------------
// Motion visualization (debug)
// ------------------------------------------------------------

/**
 * @brief Visualizes 2D motion vectors as colored arrows.
 *
 * The hue encodes direction, and the value encodes magnitude (after scaling).
 * Very small motions fall into a deadband and are shown as black.
 *
 * @param motion NDC motion vector (currNDC - prevNDC).
 * @param scale  Scalar used to amplify the vector before visualization.
 */
vec3 visualizeMotion(vec2 motion, float scale) {
    vec2 m = motion * scale;

    float mag = length(m);
    if (mag < 1e-4) {
        return vec3(0.0); // deadband → black
    }

    float hue = atan(m.y, m.x) / (2.0 * 3.1415926535) + 0.5;
    float val = clamp(mag, 0.0, 1.0);

    return hsv2rgb(vec3(hue, 1.0, val));
}

// ------------------------------------------------------------
// SVGF-lite spatial filter using VARIANCE derived from M2 in alpha
// Motion-aware + GBUFFER-aware (position + normal)
// ------------------------------------------------------------

/**
 * @brief SVGF-lite spatial filter around the current pixel.
 *
 * Uses:
 *  - M2-based variance from the history buffer (uTex) to estimate noise.
 *  - Motion magnitude to modulate filter strength (more blur on moving content).
 *  - GBuffer-based edge-stopping (position + normal) to preserve geometry edges.
 *
 * It operates on a 3x3 neighborhood and computes weights based on:
 *  - variance (wVar),
 *  - color difference (wCol),
 *  - position difference (wPos),
 *  - normal difference (wNrm),
 *  - a small spatial bias (wSpatial) that favors the center sample.
 */
vec3 svgfFilter(vec2 uv) {
    // Center sample: color + M2 of luma stored in A
    vec4 centerRaw = texture(uTex, uv);
    vec3 cCenter = centerRaw.rgb;
    float M2center = centerRaw.a;

    // Derive variance at center: Var = M2 - (E[l])^2
    float lCenter = dot(cCenter, YCOEFF);
    float varCenter = max(M2center - lCenter * lCenter, 0.0);
    varCenter = min(varCenter, uVarMax);

    // Motion at center pixel
    vec2 motion = texture(uMotionTex, uv).xy;
    float motMag = length(motion);

    // GBuffer at center
    vec3 pCenter = texture(uGPos, uv).xyz;
    vec3 nCenter = texture(uGNrm, uv).xyz;

    vec2 texel = 1.0 / uResolution;

    vec3 accumCol = vec3(0.0);
    float accumW = 0.0;

    // ----------------------------------------
    // Motion-aware tuning
    // t = 0 → static, t = 1 → fully moving
    // ----------------------------------------
    float t = clamp(smoothstep(0.005, 0.05, motMag), 0.0, 1.0);

    float kVar = mix(uKVar, uKVarMotion, t);
    float kColor = mix(uKColor, uKColorMotion, t);

    // Geometry weights – much softer so neighbors actually contribute
    const float K_NRM = 2.0;   // was 32.0 → now allows smoothing along curved surfaces
    const float K_POS = 0.02;  // was 1.0  → smoother within same object

    // Color + variance factors:
    //  - higher variance => stronger blur (not weaker)
    //  - kVar from UI still influences strength
    float varBoost = 1.0 + varCenter * (1.0 + kVar * 0.5);

    // 7x7 kernel: strong but still reasonable for real-time if resolution isn't huge
    for (int j = -3; j <= 3; ++j) {
        for (int i = -3; i <= 3; ++i) {
            vec2 offs = vec2(i, j);
            vec2 uvN = uv + offs * texel;

            // Avoid sampling outside screen
            if (uvN.x < 0.0 || uvN.x > 1.0 ||
            uvN.y < 0.0 || uvN.y > 1.0) {
                continue;
            }

            vec4 s = texture(uTex, uvN);
            vec3 c = s.rgb;

            // Color difference weight (softer than before)
            vec3 dc = c - cCenter;
            float dc2 = dot(dc, dc);
            float wCol = exp(-dc2 * (kColor * 0.3 + 0.05)); // extra 0.05 to ensure visible blur

            // GBuffer-based edge stopping
            vec3 p = texture(uGPos, uvN).xyz;
            vec3 n = texture(uGNrm, uvN).xyz;

            // Position distance
            vec3 dp = p - pCenter;
            float dist2 = dot(dp, dp);
            float wPos = exp(-dist2 * K_POS);

            // Normal difference
            float ndot = clamp(dot(normalize(nCenter), normalize(n)), -1.0, 1.0);
            float nDiff = max(0.0, 1.0 - ndot);   // 0 when aligned, 1 when opposite
            float wNrm = exp(-nDiff * K_NRM);

            // Slightly favor neighbors when variance is high,
            // but still keep center important.
            float wSpatial;
            if (i == 0 && j == 0) {
                wSpatial = 1.0;
            } else {
                // neighbors get more weight when variance is high
                wSpatial = 1.0 + varCenter * 4.0;
            }

            // Combine weights:
            //  - varBoost encourages blur in noisy areas
            //  - wCol / wPos / wNrm keep edges and big color changes
            float w = varBoost * wCol * wPos * wNrm * wSpatial;
            accumCol += c * w;
            accumW += w;
        }
    }

    if (accumW <= 0.0)
    return cCenter;

    return accumCol / accumW;
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

void main() {
    ivec2 sz = textureSize(uTex, 0);
    vec2 uv = (gl_FragCoord.xy + vec2(0.5)) / vec2(sz);

    // Motion debug mode
    if (uShowMotion == 1) {
        vec2 m = texture(uMotionTex, uv).xy;
        vec3 rgb = visualizeMotion(m, uMotionScale);
        fragColor = vec4(rgb, 1.0);
        return;
    }

    // Raw TAA result (no spatial filter)
    vec3 raw = texture(uTex, uv).rgb;

    vec3 linearColor;
    if (uEnableSVGF == 0) {
        // SVGF disabled → just use raw TAA result
        linearColor = raw;
    } else {
        // SVGF-lite: variance-guided spatial filter with GBuffer edge stopping
        vec3 filtered = svgfFilter(uv);

        // Blend between raw and filtered based on uSvgfStrength
        //  - 0.0 → pure TAA (sharp, noisy)
        //  - 1.0 → full SVGF (smooth)
        float s = clamp(uSvgfStrength, 0.0, 1.0);
        linearColor = mix(raw, filtered, s);
    }

    // Tonemap + gamma
    vec3 mapped = acesTonemap(linearColor);
    vec3 outSRGB = pow(mapped, vec3(1.0 / 2.2));

    fragColor = vec4(outSRGB, 1.0);
}