#version 450

#include "sprite.pcs"

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