#ifndef FVFX_PROJECT
#define FVFX_PROJECT

#include <stdint.h>
#include <stdbool.h>
#include "project_module.h"

#define EMPTY_MEDIA (-1)

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
    VfxAutomationKeyType type;
    double len;
    double targetValue;
} VfxLayerSoundAutomationKey;

typedef struct{
    VfxLayerSoundAutomationKey* items;
    size_t count;
    size_t capacity;
} VfxLayerSoundAutomationKeys;

typedef struct{
    double initialValue;
    VfxLayerSoundAutomationKeys keys;
} VfxLayerSoundParameter;

typedef struct{
    VfxInstanceInput* items;
    size_t count;
    size_t capacity;
} VfxInstanceInputs;

typedef struct VfxInstance VfxInstance;

struct VfxInstance{
    size_t vfx_index;
    double offset;
    double duration;
    VfxInstanceInputs inputs;
};

typedef struct{
    VfxInstance* items;
    size_t count;
    size_t capacity;
} VfxInstances;

typedef struct Layer Layer;

struct Layer{
    MediaInstances mediaInstances;
    Slices slices;
    VfxInstances vfxInstances;
    VfxLayerSoundParameter volume;
    VfxLayerSoundParameter pan;
};

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

typedef struct Project Project;

struct Project{
    Project_Settings settings;
    Layers layers;
    VfxDescriptors vfxDescriptors;
};

#endif