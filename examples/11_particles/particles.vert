#version 450
layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 vel;

layout(location = 0) out vec3 color;

void main() {
    gl_Position = vec4(pos, 0.0, 1.0);
    // Required for POINT_LIST topology.
    gl_PointSize = 2.0;
    // Slow particles cool blue, fast ones hot orange.
    float speed = length(vel);
    color = mix(vec3(0.2, 0.5, 1.0), vec3(1.0, 0.4, 0.1), clamp(speed * 1.5, 0.0, 1.0));
}
