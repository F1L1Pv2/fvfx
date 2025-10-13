#ifndef FVFX_SHADER_UTILS
#define FVFX_SHADER_UTILS

#define NOB_STRIP_PREFIX
#include "nob.h"

#include <stdint.h>

typedef enum {
    VFX_NONE = 0,
    VFX_BOOL,
    VFX_INT,
    VFX_UINT,
    VFX_FLOAT,
    VFX_DOUBLE,

    VFX_BVEC2,
    VFX_BVEC3,
    VFX_BVEC4,

    VFX_IVEC2,
    VFX_IVEC3,
    VFX_IVEC4,

    VFX_UVEC2,
    VFX_UVEC3,
    VFX_UVEC4,

    VFX_VEC2, // float vecs
    VFX_VEC3, // float vecs
    VFX_VEC4, // float vecs

    VFX_DVEC2,
    VFX_DVEC3,
    VFX_DVEC4,
} VfxInputType;

typedef struct{
    bool x;
    bool y;
} BVec2;

typedef struct{
    bool x;
    bool y;
    bool z;
} BVec3;

typedef struct{
    bool x;
    bool y;
    bool z;
    bool w;
} BVec4;

typedef struct{
    int32_t x;
    int32_t y;
} IVec2;

typedef struct{
    int32_t x;
    int32_t y;
    int32_t z;
} IVec3;

typedef struct{
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t w;
} IVec4;

typedef struct{
    uint32_t x;
    uint32_t y;
} UVec2;

typedef struct{
    uint32_t x;
    uint32_t y;
    uint32_t z;
} UVec3;

typedef struct{
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t w;
} UVec4;

typedef struct{
    float x;
    float y;
} Vec2;

typedef struct{
    float x;
    float y;
    float z;
} Vec3;

typedef struct{
    float x;
    float y;
    float z;
    float w;
} Vec4;

typedef struct{
    double x;
    double y;
} DVec2;

typedef struct{
    double x;
    double y;
    double z;
} DVec3;

typedef struct{
    double x;
    double y;
    double z;
    double w;
} DVec4;

typedef struct{
    union{
        bool Bool;
        int32_t Int;
        uint32_t Uint;
        float Float;
        double Double;

        BVec2 Bvec2;
        BVec3 Bvec3;
        BVec4 Bvec4;

        IVec2 Ivec2;
        IVec3 Ivec3;
        IVec4 Ivec4;

        UVec2 Uvec2;
        UVec3 Uvec3;
        UVec4 Uvec4;

        Vec2 vec2;
        Vec3 vec3;
        Vec4 vec4;

        DVec2 Dvec2;
        DVec3 Dvec3;
        DVec4 Dvec4;
    } as;
} VfxInputValue;

typedef struct{
    size_t push_constant_offset;
    VfxInputType type;
    const char* name;
    VfxInputValue* defaultValue;
} VfxInput;

typedef struct{
    VfxInput* items;
    size_t count;
    size_t capacity;
} VfxInputs;

typedef struct {
    const char* filepath;
    const char* name;
    const char* description;
    const char* author;
    VfxInputs inputs;
    size_t pushContantsSize;
    bool hasDefaultValues;
} VfxModule;

char* get_vfxInputTypeName(VfxInputType type);
size_t get_vfxInputTypeSize(VfxInputType type);
bool extractVFXModuleMetaData(String_View sv, VfxModule* out);
bool preprocessVFXModule(String_Builder* sb, VfxModule* module);

void lerpVfxValue(VfxInputType type, VfxInputValue* out, VfxInputValue* a, VfxInputValue* b, double t);

#endif