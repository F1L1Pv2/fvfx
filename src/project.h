#ifndef FVFX_PROJECT
#define FVFX_PROJECT

#include <stdint.h>
#include <stdbool.h>
#include "engine/platform_exporter.h"

#define EMPTY_MEDIA (-1)

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
    size_t media_index;
    double offset;
    double duration;
} Slice;

typedef struct{
    Slice* items;
    size_t count;
    size_t capacity;
} Slices;

typedef struct{
    const char* filename;
} MediaInstance;

typedef struct{
    MediaInstance* items;
    size_t count;
    size_t capacity;
} MediaInstances;

typedef enum{
    VFX_AUTO_KEY_LINEAR = 0,
    VFX_AUTO_KEY_STEP,
    VFX_AUTO_KEY_COUNT
} VfxAutomationKeyType;

typedef struct{
    VfxAutomationKeyType type;
    double len;
    VfxInputValue targetValue;
} VfxAutomationKey;

typedef struct{
    VfxAutomationKey* items;
    size_t count;
    size_t capacity;
} VfxAutomationKeys;

typedef struct{
    size_t index;
    VfxInputType type;
    VfxInputValue initialValue;
    VfxAutomationKeys keys;
} VfxInstanceInput;

typedef struct{
    VfxInstanceInput* items;
    size_t count;
    size_t capacity;
} VfxInstanceInputs;

typedef struct{
    size_t vfx_index;
    double offset;
    double duration;
    VfxInstanceInputs inputs;
} VfxInstance;

typedef struct{
    VfxInstance* items;
    size_t count;
    size_t capacity;
} VfxInstances;

typedef struct{
    MediaInstances mediaInstances;
    Slices slices;
    VfxInstances vfxInstances;
} Layer;

typedef struct{
    Layer* items;
    size_t count;
    size_t capacity;
} Layers;

typedef struct{
    const char* filename;
} VfxDescriptor;

typedef struct{
    VfxDescriptor* items;
    size_t count; 
    size_t capacity;
} VfxDescriptors;

typedef struct{
    const char* outputFilename;
    size_t width;
    size_t height;
    float fps;
    float sampleRate;
    bool hasAudio;
    bool stereo;
    Layers layers;
    VfxDescriptors vfxDescriptors;
} Project;

EXPORT_FN bool project_init(Project* project, int argc, const char** argv); // for dlls
EXPORT_FN void project_clean(Project* project); // for dlls (not mandatory doesnt need to exist)

#define VFX_COL_HEX(c) ((Vec4){ \
    .x = ((float)(((c) >> 16) & 0xFF) / 255.0f), \
    .y = ((float)(((c) >>  8) & 0xFF) / 255.0f), \
    .z = ((float)(((c) >>  0) & 0xFF) / 255.0f), \
    .w = ((float)(((c) >> 24) & 0xFF) / 255.0f)  \
})

#endif