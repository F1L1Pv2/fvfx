#version 450

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

layout (set = 0, binding = 0) uniform sampler2D textures[];

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 uv;
layout(location = 1) in flat uint InstanceIndex;

void main() {
    uint textureID = pcs.spriteDrawBuffer.commands[InstanceIndex].textureID;
    if(textureID == -1){
        outColor = vec4(pcs.spriteDrawBuffer.commands[InstanceIndex].albedo,1.0f);
    }else{
        outColor = texture(textures[textureID], uv);
    }

}