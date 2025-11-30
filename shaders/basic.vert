#version 410 core

/*
    simple_color.vert

    Minimal vertex shader for rendering simple meshes
    (e.g., the point-light proxy sphere or any debug geometry).

    Transforms input positions using the standard Model → View → Projection chain.
*/

layout (location = 0) in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}