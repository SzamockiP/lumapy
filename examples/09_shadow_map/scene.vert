#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(set = 0, binding = 0) uniform UBO {
    mat4 mvp;
    mat4 lightMvp;
} ubo;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec4 lightSpacePos;

void main() {
    gl_Position = ubo.mvp * vec4(inPosition, 1.0);
    normal = inNormal;
    // The same matrix the shadow pass rasterized with, so the lookup below is
    // consistent by construction.
    lightSpacePos = ubo.lightMvp * vec4(inPosition, 1.0);
}
