#version 410 core

/*
    simple_color.frag

    Minimal passthrough fragment shader.
    Used for debug drawing (e.g., drawing proxy geometry like the point
    light sphere or other simple overlays). The output is simply the
    uniform color provided by the CPU side.

    Inputs:
      - uColor: RGB color from C++ (alpha forced to 1.0)

    Outputs:
      - FragColor: linear RGB color
*/

out vec4 FragColor;

// Uniform color provided by the CPU
uniform vec3 uColor;

void main()
{
    FragColor = vec4(uColor, 1.0);
}