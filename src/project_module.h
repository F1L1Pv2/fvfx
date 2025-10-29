#ifndef FVFX_PROJECT_MODULE
#define FVFX_PROJECT_MODULE

#include <stdint.h>
#include <stdbool.h>
#include "engine/platform_exporter.h"

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

typedef enum{
    VFX_AUTO_KEY_LINEAR = 0,
    VFX_AUTO_KEY_STEP,
    VFX_AUTO_KEY_COUNT
} VfxAutomationKeyType;

#define VFX_COL_HEX(c) ((Vec4){ \
    .x = ((float)(((c) >> 16) & 0xFF) / 255.0f), \
    .y = ((float)(((c) >>  8) & 0xFF) / 255.0f), \
    .z = ((float)(((c) >>  0) & 0xFF) / 255.0f), \
    .w = ((float)(((c) >> 24) & 0xFF) / 255.0f)  \
})

typedef struct{
    const char* outputFilename;
    size_t width;
    size_t height;
    float fps;
    float sampleRate;
    bool hasAudio;
    bool stereo;
} Project_Settings;

typedef struct Project Project;
typedef struct Layer Layer;
typedef struct VfxInstance VfxInstance;

typedef struct{
    Project* project;

    void (*project_set_settings)(Project* project, Project_Settings settings);
    size_t (*project_add_vfx)(Project* project, const char* filename); //  returns vfx index
    Layer* (*project_create_and_add_layer)(Project* project, double initial_volume, double initial_panning); // creates layer inside project and returns ref to it
    size_t (*layer_add_media)(Project* project, Layer* layer, const char* filename); // returns media index
    void (*layer_add_slice)(Project* project, Layer* layer, size_t media_index, double media_start_from, double slice_duration);
    void (*layer_add_empty)(Project* project, Layer* layer, double empty_duration);
    VfxInstance* (*layer_create_and_add_vfx_instance)(Project* project, Layer* layer, size_t vfx_index, double instance_when, double instance_duration); // creates vfx instance inside project and returns ref to it
    void (*layer_add_volume_automation_key)(Project* project, Layer* layer, VfxAutomationKeyType automation_key_type, double automation_duration, double target_value);
    void (*layer_add_pan_automation_key)(Project* project, Layer* layer, VfxAutomationKeyType automation_key_type, double automation_duration, double target_value);
    void (*vfx_instance_set_arg)(Project* project, VfxInstance* vfx_instance, size_t input_index, VfxInputType input_type, VfxInputValue input_value);
    void (*vfx_instance_add_automation_key)(Project* project, VfxInstance* vfx_instance, size_t input_index, VfxAutomationKeyType automation_key_type, double automation_duration, VfxInputValue target_value);
} Module;

EXPORT_FN bool project_init(Module* module, int argc, const char** argv); // for dlls

#endif