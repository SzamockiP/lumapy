#version 450

// Deferred-style composite: light the albedo using the g-buffer normal.

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D gNormal;
layout(set = 0, binding = 1) uniform sampler2D gAlbedo;

const vec3 LIGHT_DIR = normalize(vec3(0.5, 0.8, 0.6));

void main() {
    vec4 n = texture(gNormal, uv);
    vec3 albedo = texture(gAlbedo, uv).rgb;

    if (n.a == 0.0) {
        // Background: nothing was rasterized here.
        outColor = vec4(0.05, 0.07, 0.1, 1.0);
        return;
    }

    float diffuse = max(dot(normalize(n.xyz), LIGHT_DIR), 0.0);
    outColor = vec4(albedo * (0.2 + 0.8 * diffuse), 1.0);
}
