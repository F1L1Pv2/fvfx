#version 450

layout(location = 0) out vec2 uv;

void main() {
    uint b = 1 << (gl_VertexIndex % 6);
    vec2 baseCoord = vec2((0x1C & b) != 0, (0xE & b) != 0);
    uv = baseCoord;

    gl_Position = vec4(baseCoord * 2 - 1, 0.0f, 1.0f);
}