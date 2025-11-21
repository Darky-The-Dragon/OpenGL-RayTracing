#version 410 core
in vec2 vUV;                    // kept for compatibility, not used for sampling
out vec4 fragColor;

uniform sampler2D uTex;        // history buffer: rgb = color, a = M2 (second moment of luma)
uniform sampler2D uMotionTex;  // RG16F, NDC motion (currNDC - prevNDC)

// GBuffer samplers
uniform sampler2D uGPos;       // world-space position
uniform sampler2D uGNrm;       // world-space normal

uniform float uExposure;
uniform int uShowMotion;   // 0 = normal, 1 = visualize motion
uniform float uMotionScale;  // e.g. 4.0
uniform vec2 uResolution;   // framebuffer size in pixels

// Declared but not used yet (safe)
uniform float uVarMax;
uniform float uKVar;
uniform float uKColor;
uniform float uKVarMotion;
uniform float uKColorMotion;
uniform float uSvgfStrength;
uniform float uSvgfVarStaticEps;
uniform float uSvgfMotionStaticEps;

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
vec3 svgfFilter(vec2 uv) {
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

    // GBuffer at center
    vec3 pCenter = texture(uGPos, uv).xyz;
    vec3 nCenter = texture(uGNrm, uv).xyz;

    // Clamp variance to a reasonable max – prevents insane weights
    varCenter = min(varCenter, uVarMax);

    // If variance is essentially tiny *and* motion is tiny → skip blur.
    if (varCenter < uSvgfVarStaticEps && motMag < uSvgfMotionStaticEps) {
        return cCenter;
    }

    vec2 texel = 1.0 / uResolution;

    vec3 accumCol = vec3(0.0);
    float accumW = 0.0;

    // ----------------------------------------
    // Motion-aware filter strength, driven by CPU params
    // uKVar / uKColor control the *overall* sharpness vs blur.
    // We derive static/moving variants from them.
    // ----------------------------------------

    // t = 0 → static, t = 1 → fully moving
    float t = clamp(smoothstep(0.005, 0.05, motMag), 0.0, 1.0);

    float kVar = mix(uKVar, uKVarMotion, t);
    float kColor = mix(uKColor, uKColorMotion, t);

    // Geometry weights – fixed scalars for now (no extra CPU params needed)
    const float K_NRM = 32.0;   // higher = stronger normal edge stopping
    const float K_POS = 1.0;    // units are ~1 / (worldUnit^2)

    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 offs = vec2(i, j);
            vec2 uvN = uv + offs * texel;

            // Avoid sampling outside screen
            if (uvN.x < 0.0 || uvN.x > 1.0 ||
            uvN.y < 0.0 || uvN.y > 1.0) {
                continue;
            }

            vec4 s = texture(uTex, uvN);
            vec3 c = s.rgb;
            float M2 = s.a;

            float l = dot(c, YCOEFF);
            float v = max(M2 - l * l, 0.0);
            v = min(v, uVarMax);

            float wVar = exp(-v * kVar);

            vec3 dc = c - cCenter;
            float dc2 = dot(dc, dc);
            float wCol = exp(-dc2 * kColor);

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

            // Slightly favor center when static; when moving, neighbors matter more
            float wSpatial = (i == 0 && j == 0)
            ? 1.0
            : mix(0.8, 1.1, t);

            float w = wVar * wCol * wPos * wNrm * wSpatial;
            accumCol += c * w;
            accumW += w;
        }
    }

    if (accumW <= 0.0)
    return cCenter;

    return accumCol / accumW;
}

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

    // SVGF-lite: variance-guided spatial filter with GBuffer edge stopping
    vec3 filtered = svgfFilter(uv);

    // Blend between raw and filtered based on uSvgfStrength
    //  - 0.0 → pure TAA (sharp, noisy)
    //  - 1.0 → full SVGF (smooth, possibly darker/softer)
    //  - in-between → compromise
    float s = clamp(uSvgfStrength, 0.0, 1.0);
    vec3 linearColor = mix(raw, filtered, s);

    // Tonemap + gamma
    vec3 mapped = acesTonemap(linearColor);
    vec3 outSRGB = pow(mapped, vec3(1.0 / 2.2));

    fragColor = vec4(outSRGB, 1.0);
}