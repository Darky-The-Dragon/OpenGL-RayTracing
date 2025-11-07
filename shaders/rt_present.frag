#version 410 core
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uTex;
uniform float     uExposure;

// ACES approximation (Narkowicz 2015)
vec3 acesTonemap(vec3 x) {
    // optional exposure in linear BEFORE tonemap
    x = x * uExposure;
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x*(a*x + b)) / (x*(c*x + d) + e), 0.0, 1.0);
}

void main() {
    // Accum texture is LINEAR
    vec3 col = texture(uTex, vUV).rgb;

    // Tone map + gamma encode
    col = acesTonemap(col);
    col = pow(col, vec3(1.0/2.2));

    fragColor = vec4(col, 1.0);
}
