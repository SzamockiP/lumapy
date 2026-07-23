#version 450
// Reconstructs a world-space ray per pixel from the camera basis (avoids any
// clip-space convention wrangling) and samples the cubemap along it.
layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube sky;

layout(push_constant) uniform PC {
    vec4 right;    // camera basis vectors (xyz)
    vec4 up;
    vec4 forward;
    vec4 params;   // params.x = tan(fov/2), params.y = aspect
} pc;

void main() {
    // Vulkan NDC y points down, so negate it to get world-up.
    vec3 dir = normalize(
        pc.forward.xyz
        + ndc.x * pc.params.y * pc.params.x * pc.right.xyz
        - ndc.y * pc.params.x * pc.up.xyz);
    outColor = texture(sky, dir);
}
