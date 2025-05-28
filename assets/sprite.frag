#version 450

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 uv;

void main() {
    outColor = vec4(uv, 0.0f,1.0f);
}