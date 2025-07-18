#version 450

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 uv;

layout (set = 0, binding = 0) uniform sampler2D imageIN;

void main() {
    outColor = texture(imageIN, uv);
}