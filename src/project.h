#ifndef FVFX_PROJECT
#define FVFX_PROJECT

#include "shader_utils.h"

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

#endif