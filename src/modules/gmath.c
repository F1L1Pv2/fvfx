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
        S/aspect,  0,          0            ,            0,
            0   , -S,          0            ,            0,
            0   ,  0, -zFar / (zFar - zNear), -zFar * zNear / (zFar - zNear),
            0   ,  0,         -1            ,            0,
    };
}