#version 410 core
in vec2 vUV;                    // kept for compatibility, not used for sampling
out vec4 fragColor;

uniform sampler2D uTex;        // history buffer: rgb = color, a = M2 (second moment of luma)
uniform sampler2D uMotionTex;  // RG16F, NDC motion (currNDC - prevNDC)
uniform float uExposure;
uniform int uShowMotion;     // 0 = normal, 1 = visualize motion
uniform float uMotionScale;    // e.g. 4.0
uniform vec2 uResolution;     // framebuffer size in pixels

// Luma coefficients (approx. Rec.709)
const vec3 YCOEFF = vec3(0.299, 0.587, 0.114);

// ACES approximation (Narkowicz 2015)
vec3 acesTonemap(vec3 x) {
    x *= uExposure;
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// HSV → RGB
vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

// ------------------------------------------------------------
// Motion visualization (debug)
// ------------------------------------------------------------
vec3 visualizeMotion(vec2 motion, float scale)
{
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
// Motion-aware: stronger filter when things are moving
// ------------------------------------------------------------
vec3 svgfFilter(vec2 uv)
{
    // Center sample: color + M2 of luma stored in A
    vec4 centerRaw = texture(uTex, uv);
    vec3 cCenter = centerRaw.rgb;
    float M2center = centerRaw.a;

    // Derive variance at center: Var = M2 - (E[l])^2
    float lCenter = dot(cCenter, YCOEFF);
    float varCenter = max(M2center - lCenter * lCenter, 0.0);

    // Motion at center pixel
    vec2 motion = texture(uMotionTex, uv).xy;
    float motMag = length(motion);

    // Clamp variance to a reasonable max – prevents insane weights
    const float VAR_MAX = 0.02;  // tweak: 0.01–0.05
    varCenter = min(varCenter, VAR_MAX);

    // If variance is essentially zero *and* motion is tiny → already stable
    // → skip blur completely (sharp and stable).
    if (varCenter < 1e-8 && motMag < 0.002) {
        return cCenter;
    }

    vec2 texel = 1.0 / uResolution;

    vec3 accumCol = vec3(0.0);
    float accumW = 0.0;

    // ----------------------------------------
    // Motion-aware filter strength
    // - When static: conservative (keep detail)
    // - When moving: stronger smoothing (hide noise)
    // ----------------------------------------
    const float kVarStatic = 220.0;
    const float kColorStatic = 22.0;

    const float kVarMoving = 80.0;
    const float kColorMoving = 8.0;

    // t = 0 → static, t = 1 → fully moving
    float t = clamp(smoothstep(0.01, 0.08, motMag), 0.0, 1.0);

    float kVar = mix(kVarStatic, kVarMoving, t);
    float kColor = mix(kColorStatic, kColorMoving, t);

    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 offs = vec2(i, j);
            vec2 uvN = uv + offs * texel;

            // Avoid sampling outside screen
            if (uvN.x < 0.0 || uvN.x > 1.0 ||
            uvN.y < 0.0 || uvN.y > 1.0)
            continue;

            vec4 s = texture(uTex, uvN);
            vec3 c = s.rgb;
            float M2 = s.a;

            // Derive neighbor variance from its color + M2
            float l = dot(c, YCOEFF);
            float v = max(M2 - l * l, 0.0);
            v = min(v, VAR_MAX);

            // Variance weight: high variance → lower weight
            float wVar = exp(-v * kVar);

            // Color weight: avoid blurring across strong edges
            vec3 dc = c - cCenter;
            float dc2 = dot(dc, dc);
            float wCol = exp(-dc2 * kColor);

            // Slightly favor the center sample
            float wSpatial = (i == 0 && j == 0) ? 1.0 : 0.8;

            float w = wVar * wCol * wSpatial;
            accumCol += c * w;
            accumW += w;
        }
    }

    if (accumW <= 0.0)
    return cCenter;

    return accumCol / accumW;
}

void main() {
    // Pixel-exact UV built from gl_FragCoord to avoid full-screen triangle seams
    ivec2 sz = textureSize(uTex, 0);
    vec2 uv = (gl_FragCoord.xy + vec2(0.5)) / vec2(sz);

    // Motion debug mode
    if (uShowMotion == 1) {
        vec2 m = texture(uMotionTex, uv).xy;
        vec3 rgb = visualizeMotion(m, uMotionScale);
        fragColor = vec4(rgb, 1.0);
        return;
    }

    // SVGF-lite: variance-guided spatial filter, then ACES + gamma
    vec3 filtered = svgfFilter(uv);
    vec3 mapped = acesTonemap(filtered);
    vec3 outSRGB = pow(mapped, vec3(1.0 / 2.2));

    fragColor = vec4(outSRGB, 1.0);
}