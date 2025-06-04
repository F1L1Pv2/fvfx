#version 450

layout (push_constant) uniform constants
{
    mat4 proj;
    mat4 view;
    mat4 model;
} pcs;

layout (location = 0) in vec3 pos;
layout (location = 1) in vec3 normal;

layout (location = 0) out flat uint VertexIndex;

void main() {
    gl_Position = pcs.proj * pcs.view * pcs.model * vec4(pos,1.0f);

    VertexIndex = gl_VertexIndex;
}