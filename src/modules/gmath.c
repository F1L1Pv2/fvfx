#include "gmath.h"

#include "math.h"

mat4 ortho2D(float width, float height){
    float left = -width/2;
    float right = width/2;
    float top = height/2;
    float bottom = -height/2;

    return (mat4){
    2 / (right - left),0                 , 0, -(right + left) / (right - left),
          0           ,2 / (top - bottom), 0, -(top + bottom) / (top - bottom),
          0           ,     0            ,-1,                 0,
          0           ,     0            , 0,                 1,
    };
}

mat4 perspective(float fov, float aspect, float zNear, float zFar){
    float S = 1.0f / tanf(fov*PI/180/2);
    return (mat4){
        S/aspect,  0,             0                 ,     0,
            0   , -S,             0                 ,     0,
            0   ,  0,    -zFar / (zFar - zNear)     ,    -1,
            0   ,  0, -zFar * zNear / (zFar - zNear),     0,
    };
}

mat4 mat4mul(mat4 it, mat4 other) {
    return (mat4){
        // First row
        it.x11 * other.x11 + it.x12 * other.x21 + it.x13 * other.x31 + it.x14 * other.x41, // m11
        it.x11 * other.x12 + it.x12 * other.x22 + it.x13 * other.x32 + it.x14 * other.x42, // m12
        it.x11 * other.x13 + it.x12 * other.x23 + it.x13 * other.x33 + it.x14 * other.x43, // m13
        it.x11 * other.x14 + it.x12 * other.x24 + it.x13 * other.x34 + it.x14 * other.x44, // m14
        // Second row
        it.x21 * other.x11 + it.x22 * other.x21 + it.x23 * other.x31 + it.x24 * other.x41, // m21
        it.x21 * other.x12 + it.x22 * other.x22 + it.x23 * other.x32 + it.x24 * other.x42, // m22
        it.x21 * other.x13 + it.x22 * other.x23 + it.x23 * other.x33 + it.x24 * other.x43, // m23
        it.x21 * other.x14 + it.x22 * other.x24 + it.x23 * other.x34 + it.x24 * other.x44, // m24
        // Third row
        it.x31 * other.x11 + it.x32 * other.x21 + it.x33 * other.x31 + it.x34 * other.x41, // m31
        it.x31 * other.x12 + it.x32 * other.x22 + it.x33 * other.x32 + it.x34 * other.x42, // m32
        it.x31 * other.x13 + it.x32 * other.x23 + it.x33 * other.x33 + it.x34 * other.x43, // m33
        it.x31 * other.x14 + it.x32 * other.x24 + it.x33 * other.x34 + it.x34 * other.x44, // m34
        // Fourth row
        it.x41 * other.x11 + it.x42 * other.x21 + it.x43 * other.x31 + it.x44 * other.x41, // m41
        it.x41 * other.x12 + it.x42 * other.x22 + it.x43 * other.x32 + it.x44 * other.x42, // m42
        it.x41 * other.x13 + it.x42 * other.x23 + it.x43 * other.x33 + it.x44 * other.x43, // m43
        it.x41 * other.x14 + it.x42 * other.x24 + it.x43 * other.x34 + it.x44 * other.x44  // m44
    };
}