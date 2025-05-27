#version 450

layout(location = 0) out vec2 uv;

layout (push_constant) uniform constants
{
    vec2 offset;
    vec2 scale;
    float rotation;
} pcs;

void main() {
    uint b = 1 << (gl_VertexIndex % 6);
    vec2 baseCoord = vec2((0x1C & b) != 0, (0xE & b) != 0);
    uv = baseCoord;

    vec2 coord = baseCoord;

    mat2 rotation = mat2(cos(pcs.rotation), -sin(pcs.rotation),sin(pcs.rotation),cos(pcs.rotation));

    gl_Position = vec4(((rotation * (coord - vec2(.5,.5))) + vec2(.5,.5)) * pcs.scale + pcs.offset, 0.0f, 1.0f);
}