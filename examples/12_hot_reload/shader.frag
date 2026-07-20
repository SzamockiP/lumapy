#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D tex;

#include "palette.glsl"

void main() {
    // Edit this math, or palette.glsl, while the demo runs. Make a typo and the
    // app keeps rendering the last good version (watch the console).
    outColor = vec4(texture(tex, uv).rgb * tint(), 1.0);
}
