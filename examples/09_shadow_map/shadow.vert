#version 450

// Depth-only pass: rasterize the scene from the light's point of view.
// There is no fragment shader — the target has no colour attachments.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;  // declared in the pipeline, unused here

layout(set = 0, binding = 0) uniform UBO {
    mat4 mvp;       // camera, unused in this pass
    mat4 lightMvp;
} ubo;

void main() {
    gl_Position = ubo.lightMvp * vec4(inPosition, 1.0);
}
