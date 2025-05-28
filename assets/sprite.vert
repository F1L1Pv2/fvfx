#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout: require

struct SpriteDrawCommand {
    mat4 transform;
    uint textureID;
    vec3 albedo;
};

layout (buffer_reference, scalar) readonly buffer SpriteDrawBuffer {
    SpriteDrawCommand commands[];
};

layout (push_constant) uniform constants
{
    mat4 proj;
    mat4 view;
    SpriteDrawBuffer spriteDrawBuffer;
} pcs;

layout(location = 0) out vec2 uv;
layout(location = 1) out flat uint InstanceIndex;

void main() {
    uint b = 1 << (gl_VertexIndex % 6);
    vec2 baseCoord = vec2((0x1C & b) != 0, (0xE & b) != 0);
    uv = baseCoord;

    gl_Position = pcs.proj * pcs.view * pcs.spriteDrawBuffer.commands[gl_InstanceIndex].transform * vec4(baseCoord, 0.0f, 1.0f);
    InstanceIndex = gl_InstanceIndex;
}