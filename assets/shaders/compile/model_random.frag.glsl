#version 450

layout(location = 0) out vec4 outColor;

layout (location = 0) in flat uint Index;

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float val = float(Index);
    outColor = vec4(rand(vec2(val, 11)),rand(vec2(val, 7)),rand(vec2(val, 3)), 1.0);
}