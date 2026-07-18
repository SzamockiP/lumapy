#version 450

// Two colour outputs, one per attachment — the minimal MRT shader.

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 outA;
layout(location = 1) out vec4 outB;

void main() {
    outA = vec4(0.25, 0.5, 0.75, 1.0);
    outB = vec4(1.0, 0.0, 1.0, 1.0);
}
