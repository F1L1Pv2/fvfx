#ifndef MODULES_GMATH
#define MODULES_GMATH

#define PI 3.14159265358979323846

typedef struct {
    float x;
    float y;
} vec2;

typedef struct {
    float x;
    float y;
    float z;
} vec3;

typedef struct {
    float x;
    float y;
    float z;
    float w;
} vec4;

typedef struct {
    float 
        x11, x21, x31, x41,    //  x11 | x12 | x13 | x14
        x12, x22, x32, x42,    //  x21 | x22 | x23 | x24
        x13, x23, x33, x43,    //  x31 | x32 | x33 | x34
        x14, x24, x34, x44;    //  x41 | x42 | x43 | x44
} mat4;

mat4 ortho2D(float width, float height);
mat4 perspective(float fov, float aspect, float zNear, float zFar);
mat4 mat4mul(mat4 it, mat4 other);

#endif