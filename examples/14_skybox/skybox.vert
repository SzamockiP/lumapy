#version 450
// Fullscreen triangle; hands the clip-space xy (NDC) to the fragment shader,
// which turns each pixel into a world-space view ray.
layout(location = 0) out vec2 ndc;

void main() {
    vec2 uv = vec2(gl_VertexIndex & 2, (gl_VertexIndex << 1) & 2);
    ndc = uv * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
