#version 450

layout (push_constant) uniform constants
{
    mat4 proj;
    mat4 view;
    mat4 model;
} pcs;

layout (location = 0) in vec3 pos;

layout (location = 0) out vec3 Normal;
layout (location = 1) out vec3 FragPos;

void main() {
    gl_Position = pcs.proj * pcs.view * pcs.model * vec4(pos,1.0f);

    Normal = pos;
    FragPos = pos;
}