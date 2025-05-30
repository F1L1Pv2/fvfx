#version 450

#include "sprite.pcs"

layout(location = 0) out vec2 uv;
layout(location = 1) out flat uint InstanceIndex;

void main() {
    uint b = 1 << (gl_VertexIndex % 6);
    vec2 baseCoord = vec2((0x1C & b) != 0, (0xE & b) != 0);
    uv = baseCoord;

    gl_Position = pcs.proj * pcs.view * pcs.spriteDrawBuffer.commands[gl_InstanceIndex].transform * vec4(baseCoord, 0.0f, 1.0f);
    InstanceIndex = gl_InstanceIndex;
}