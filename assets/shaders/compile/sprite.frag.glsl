#version 450

#include "sprite_pcs.glsl"

layout (set = 0, binding = 0) uniform sampler2D textures[];

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 uv;
layout(location = 1) in flat uint InstanceIndex;

vec3 hsv2rgb(vec3 c) {
    vec3 rgb = clamp(abs(mod(c.x * 6.0 + vec3(0.0, 4.0, 2.0),
                             6.0) - 3.0) - 1.0,
                     0.0,
                     1.0);
    rgb = rgb * rgb * (3.0 - 2.0 * rgb); // smoothstep-like
    return c.z * mix(vec3(1.0), rgb, c.y);
}

void main() {
    uint textureID = (pcs.spriteDrawBuffer.commands[InstanceIndex].textureIDEffects & 0xFFFF) - 1;
    uint effects = (pcs.spriteDrawBuffer.commands[InstanceIndex].textureIDEffects >> 2*8) & 0xFFFF;

    vec2 new_uv = (uv * pcs.spriteDrawBuffer.commands[InstanceIndex].size) + pcs.spriteDrawBuffer.commands[InstanceIndex].offset;

    // we are doing a little bit of hacks because who cares everyone knows that this code will be only use here for very specific things

    if(textureID == -1){
        if(effects == 1){ // GRADIENT HSV
            vec3 hsv = vec3(pcs.spriteDrawBuffer.commands[InstanceIndex].albedo.r, new_uv.x, 1.0 - new_uv.y);
            outColor = vec4(hsv2rgb(hsv), 1.0);
        }else{
            outColor = vec4(pcs.spriteDrawBuffer.commands[InstanceIndex].albedo, 1.0);
        }
    }else{
        if(effects == 1) // SDF
        {
            float d = texture(textures[textureID], new_uv).r;
            float aaf = fwidth(d);
            float alpha = smoothstep(0.5 - aaf, 0.5 + aaf, d);
            outColor = vec4(pcs.spriteDrawBuffer.commands[InstanceIndex].albedo, alpha);
        }else{
            outColor = texture(textures[textureID], new_uv);
            if(effects == 2){
                outColor *= vec4(pcs.spriteDrawBuffer.commands[InstanceIndex].albedo, 1.0);
            }
        }
    }

}