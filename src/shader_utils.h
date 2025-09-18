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

    VFX_COLOR,
} VfxInputType;

typedef struct{
    VfxInputType type;
    const char* name;
    void* defaultPushConstantValue;
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
    void* defaultPushConstantValue;
} VfxModule;

char* get_vfxInputTypeName(VfxInputType type);
size_t get_vfxInputTypeSize(VfxInputType type);
bool extractVFXModuleMetaData(String_View sv, VfxModule* out);
bool preprocessVFXModule(String_Builder* sb, VfxModule* module);
//Assumes buff has enough size
void vfx_fill_default_values(VfxModule* module, void* buff);

#endif