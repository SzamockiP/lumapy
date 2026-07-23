#version 450
// Samples a cubemap in a direction pushed as a constant and writes it flat.
// A headless test reads back one face per direction; each face is a solid
// colour in these tests, so the exact point within a face doesn't matter.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube sky;
layout(push_constant) uniform PC { vec4 dir; } pc;

void main() {
    outColor = texture(sky, pc.dir.xyz);
}
