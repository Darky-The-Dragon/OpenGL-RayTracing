#version 410 core
#define ENABLE_SVGF 1   // set to 0 to disable spatial denoiser for debugging

in vec2 vUV;                    // kept for compatibility, not used for sampling
out vec4 fragColor;

uniform sampler2D uTex;        // history buffer: rgb = color, a = variance
uniform sampler2D uMotionTex;  // RG16F, NDC motion (currNDC - prevNDC)
uniform float uExposure;
uniform int   uShowMotion;     // 0 = normal, 1 = visualize motion
uniform float uMotionScale;    // e.g. 4.0
uniform vec2  uResolution;     // framebuffer size in pixels

// ACES approximation (Narkowicz 2015)
vec3 acesTonemap(vec3 x) {
    x *= uExposure;
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// HSV → RGB
vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0);
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

vec3 svgfFilter(vec2 uv)
{
    // Center sample: color + variance
    vec4 centerRaw  = texture(uTex, uv);
    vec3 cCenter    = centerRaw.rgb;
    float varCenter = centerRaw.a;

    // Clamp variance to a reasonable range – prevents insane weights
    const float VAR_MAX = 0.02;  // tweak: 0.01–0.05
    varCenter = clamp(varCenter, 0.0, VAR_MAX);

    // If variance is essentially zero → already stable → skip blur
    if (varCenter < 1e-8)
    return cCenter;

    vec2 texel = 1.0 / uResolution;

    vec3 accumCol = vec3(0.0);
    float accumW  = 0.0;

    // Tunable parameters
    const float kVar   = 200.0;  // variance falloff
    const float kColor = 20.0;   // color difference falloff

    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 offs = vec2(i, j);
            vec2 uvN  = uv + offs * texel;

            // Avoid sampling outside screen
            if (uvN.x < 0.0 || uvN.x > 1.0 ||
            uvN.y < 0.0 || uvN.y > 1.0)
            continue;

            vec4 s = texture(uTex, uvN);
            vec3 c = s.rgb;
            float v = s.a;

            // Clamp neighbor variance as well
            v = clamp(v, 0.0, VAR_MAX);

            // Variance weight: high variance → lower weight
            float wVar = exp(-v * kVar);

            // Color weight: avoid blurring across strong edges
            vec3 dc  = c - cCenter;
            float dc2 = dot(dc, dc);
            float wCol = exp(-dc2 * kColor);

            // Slightly favor the center sample
            float wSpatial = (i == 0 && j == 0) ? 1.0 : 0.8;

            float w = wVar * wCol * wSpatial;
            accumCol += c * w;
            accumW   += w;
        }
    }

    if (accumW <= 0.0)
    return cCenter;

    return accumCol / accumW;
}

void main() {
    // Pixel-exact UV built from gl_FragCoord to avoid full-screen triangle seams
    ivec2 sz = textureSize(uTex, 0);
    vec2  uv = (gl_FragCoord.xy + vec2(0.5)) / vec2(sz);

    // Motion debug mode
    if (uShowMotion == 1) {
        vec2 m   = texture(uMotionTex, uv).xy;
        vec3 rgb = visualizeMotion(m, uMotionScale);
        fragColor = vec4(rgb, 1.0);
        return;
    }

    // SVGF-lite: variance-guided spatial filter, then ACES + gamma
    #if ENABLE_SVGF
    vec3 filtered = svgfFilter(uv);
    #else
    // Debug path: no spatial denoiser, just TAA output
    vec3 filtered = texture(uTex, uv).rgb;
    #endif

    vec3 mapped   = acesTonemap(filtered);
    vec3 outSRGB  = pow(mapped, vec3(1.0 / 2.2));

    fragColor = vec4(outSRGB, 1.0);
}