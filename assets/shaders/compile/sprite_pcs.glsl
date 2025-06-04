#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout: require
#extension GL_EXT_nonuniform_qualifier: require

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