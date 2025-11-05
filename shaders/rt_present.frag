#version 410 core
in vec2 vUV; out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    // Accum texture is LINEAR; convert to display gamma here
    vec3 col = texture(uTex, vUV).rgb;
    col = pow(col, vec3(1.0/2.2));   // gamma encode for the screen
    fragColor = vec4(col, 1.0);
}
