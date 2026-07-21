#version 450

// Fullscreen triangle from gl_VertexIndex — no vertex buffer. UV ordering
// matches tests/shaders/fullscreen.vert so the winding is front-facing under
// the pipeline default (cull BACK, front face COUNTER_CLOCKWISE).

layout(location = 0) out vec2 uv;

void main() {
    uv = vec2(gl_VertexIndex & 2, (gl_VertexIndex << 1) & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
