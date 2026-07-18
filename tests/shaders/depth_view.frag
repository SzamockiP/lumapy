#version 450

// Visualizes a depth texture: the raw depth value becomes a grayscale colour.
// Sampling a depth image through a plain sampler2D returns depth in .r.

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D depthTex;

void main() {
    float d = texture(depthTex, uv).r;
    outColor = vec4(d, d, d, 1.0);
}
