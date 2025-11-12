#version 410 core
in vec2 vUV;                    // kept for compatibility, not used for sampling
out vec4 fragColor;

uniform sampler2D uTex;        // accumulated linear color (TAA output)
uniform sampler2D uMotionTex;  // RG16F, NDC motion (currNDC - prevNDC)
uniform float uExposure;
uniform int   uShowMotion;     // 0 = normal, 1 = visualize motion
uniform float uMotionScale;    // e.g. 4.0

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

void main() {
    // Pixel-exact UV built from gl_FragCoord to avoid full-screen triangle seams
    ivec2 sz = textureSize(uTex, 0);
    vec2  uv = (gl_FragCoord.xy + vec2(0.5)) / vec2(sz);

    if (uShowMotion == 1) {
        vec2 m = texture(uMotionTex, uv).xy;  // use pixel UV, not interpolated vUV
        m *= uMotionScale;

        // Deadband so perfect rest is black
        float mag = length(m);
        if (mag < 1e-4) {
            fragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        // Hue = direction, Value = magnitude (clamped), Saturation = 1
        float hue = atan(m.y, m.x) / (2.0 * 3.1415926535) + 0.5;
        float val = clamp(mag, 0.0, 1.0);
        vec3  rgb = hsv2rgb(vec3(hue, 1.0, val));
        fragColor = vec4(rgb, 1.0);
        return;
    }

    // Normal present path (ACES + gamma) using pixel UV
    vec3 lin     = texture(uTex, uv).rgb;
    vec3 mapped  = acesTonemap(lin);
    vec3 outSRGB = pow(mapped, vec3(1.0/2.2));
    fragColor = vec4(outSRGB, 1.0);
}