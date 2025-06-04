#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout: require
#extension GL_EXT_nonuniform_qualifier: require

struct Tetrahedron {
    vec4 v[4];
};

layout (buffer_reference, scalar) readonly buffer Mesh4D {
    Tetrahedron vertices[];
};

layout (push_constant) uniform constants
{
    mat4 proj;
    mat4 view;
    mat4 ModelMatrix;
    vec4 CamPosition;
    Mesh4D mesh4D;
} pcs;

layout (set = 0, binding = 0) uniform sampler2D LUT;

layout (location = 0) out flat uint Index;
layout (location = 1) out vec4 uv;

#define ModelToCam(V) V = (pcs.ModelMatrix *(V + pcs.CamPosition))
#define CamToModel(V) V = ((V * pcs.ModelMatrix) - pcs.CamPosition)

void main() {
    Tetrahedron vertex = pcs.mesh4D.vertices[gl_InstanceIndex];

    ModelToCam(vertex.v[0]);
    ModelToCam(vertex.v[1]);
    ModelToCam(vertex.v[2]);
    ModelToCam(vertex.v[3]);

    float x = (gl_VertexIndex % 4) + (vertex.v[0].w > 0.0 ? 4 : 0);
    float y = (vertex.v[1].w > 0.0 ? 1 : 0) + (vertex.v[2].w > 0.0 ? 2 : 0) + (vertex.v[3].w > 0.0 ? 4 : 0);
    vec4 lookup = texture(LUT, vec2((x+0.5)/8.0,1.0f-(y+0.5)/8.0));
    uint ix1 = uint(lookup.r * 4.0);
    uint ix2 = uint(lookup.g * 4.0);
    vec4 v1 = vertex.v[ix1];
    vec4 v2 = vertex.v[ix2];

    v1.w = clamp(v1.w / (v1.w - v2.w), 0.0, 1.0);
    v2.w = 1.0 - v1.w;

    gl_Position.xyz = v1.xyz * v2.w + v2.xyz * v1.w;

    uv = vec4(gl_Position.xyz, 0.0);
    CamToModel(uv);

    gl_Position = pcs.view * vec4(gl_Position.x, gl_Position.y, -gl_Position.z, 1.0);
    gl_Position = pcs.proj * vec4(gl_Position.x, gl_Position.y, gl_Position.z, 1.0f);

    Index = gl_InstanceIndex / 5;
}