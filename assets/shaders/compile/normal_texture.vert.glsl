#version 450

layout(location = 0) out vec2 uv;

layout (push_constant) uniform constants
{
    mat4 proj;
    mat4 view;
    mat4 model;
} pcs;

void main() {
    uint b = 1 << (gl_VertexIndex % 6);
    vec2 baseCoord = vec2((0x1C & b) != 0, (0xE & b) != 0);
    uv = baseCoord;

    gl_Position = pcs.proj * pcs.view * pcs.model * vec4(baseCoord, 0.0f, 1.0f);
}