#version 410 core
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uTex;        // accumulated linear color
uniform sampler2D uMotionTex;  // motion vectors (RG16F), NDC delta
uniform float uExposure;
uniform int   uShowMotion;

// ACES approximation (Narkowicz 2015)
vec3 acesTonemap(vec3 x) {
    // optional exposure in linear BEFORE tonemap
    x = x * uExposure;
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    if (uShowMotion == 1) {
        vec2 mv = texture(uMotionTex, vUV).xy;  // NDC delta
        // Visualize:
        // scale for visibility and map to color:
        // X in red, Y in green, magnitude in blue
        float scale = 4.0; // tune for your camera speed
        vec2 vis = mv * scale * 0.5 + 0.5; // center at 0.5
        float mag = clamp(length(mv) * scale, 0.0, 1.0);
        fragColor = vec4(vis.x, vis.y, mag, 1.0);
        return;
    }

    // Normal present path (ACES + gamma)
    vec3 lin = texture(uTex, vUV).rgb;
    vec3 mapped = acesTonemap(lin * uExposure);
    vec3 outSRGB = pow(mapped, vec3(1.0/2.2));
    fragColor = vec4(outSRGB, 1.0);
}