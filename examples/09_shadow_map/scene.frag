#version 450

layout(location = 0) in vec3 normal;
layout(location = 1) in vec4 lightSpacePos;

layout(location = 0) out vec4 outColor;

// shadow.depth from the RenderTarget — an ordinary sampled image.
layout(set = 0, binding = 1) uniform sampler2D shadowMap;

const vec3 LIGHT_DIR = normalize(vec3(0.5, 1.0, 0.25));

void main() {
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    vec2 uv = proj.xy * 0.5 + 0.5;

    float shadow = 1.0;
    if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0 && proj.z <= 1.0) {
        float mapDepth = texture(shadowMap, uv).r;
        if (proj.z - 0.003 > mapDepth) {
            shadow = 0.35;
        }
    }

    float diffuse = max(dot(normalize(normal), LIGHT_DIR), 0.0);
    vec3 base = vec3(0.85, 0.8, 0.7);
    outColor = vec4(base * (0.25 + 0.75 * diffuse) * shadow, 1.0);
}
