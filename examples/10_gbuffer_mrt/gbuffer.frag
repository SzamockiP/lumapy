#version 450

// One pass, two attachments: world normals into RGBA16F, albedo into RGBA8.

layout(location = 0) in vec3 normal;

layout(location = 0) out vec4 outNormal;  // RGBA16F
layout(location = 1) out vec4 outAlbedo;  // RGBA8

void main() {
    vec3 n = normalize(normal);
    outNormal = vec4(n, 1.0);
    // Face colour derived from the normal, so the attachments visibly differ.
    outAlbedo = vec4(abs(n) * 0.8 + 0.2, 1.0);
}
