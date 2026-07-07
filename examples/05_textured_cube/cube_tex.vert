#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(set = 0, binding = 0) uniform UBO {
    mat4 mvp;
} ubo;

layout(location = 0) out vec2 fragUV;

void main() {
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    fragUV = inUV;
}
