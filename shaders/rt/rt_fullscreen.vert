#version 410 core

/*
    Fullscreen Triangle Vertex Shader

    This shader generates a fullscreen triangle without requiring any vertex
    buffers or VAOs. Instead, gl_VertexID is used to procedurally define
    three clip-space positions that cover the entire screen:

        ( -1, -1 )
        (  3, -1 )
        ( -1,  3 )

    These three points form a single triangle that fully contains the
    canonical viewport rectangle [-1, 1]². This is a common optimization
    in modern rendering pipelines because:

    - It avoids sending a fullscreen quad (two triangles) through the pipeline.
    - It eliminates redundant vertex data, VAOs, and buffer state.
    - UV computation becomes trivial using the generated clip-space coordinates.

    The shader outputs:
    - vUV: UV coordinates in [0,1]² derived from clip-space position.
    - gl_Position: final clip-space position of the fullscreen triangle.
*/

out vec2 vUV;

void main() {
    vec2 p;

    // Procedural vertex positions based on gl_VertexID
    if (gl_VertexID == 0) {
        p = vec2(-1.0, -1.0);
    }
    else if (gl_VertexID == 1) {
        p = vec2(3.0, -1.0);
    }
    else {
        p = vec2(-1.0, 3.0);
    }

    // Convert clip-space coordinates ([-1,1]) to UV ([0,1])
    vUV = 0.5 * (p + 1.0);

    // Final screen-space output
    gl_Position = vec4(p, 0.0, 1.0);
}