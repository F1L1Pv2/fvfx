#ifndef FVFX_PROJECT
#define FVFX_PROJECT

#include <stdint.h>
#include <stdbool.h>
#include "project_module.h"
#include "arena_alloc.h"

#define EMPTY_MEDIA (-1)

typedef struct Slice Slice;
struct Slice{
    size_t media_index;
    double offset;
    double duration;
    Slice* next;
};

typedef struct MediaInstance MediaInstance;
struct MediaInstance{
    const char* filename;
    MediaInstance* next;
};

typedef struct VfxAutomationKey VfxAutomationKey;
struct VfxAutomationKey{
    VfxAutomationKeyType type;
    double len;
    VfxInputValue targetValue;
    VfxAutomationKey* next;
};

typedef struct VfxInstanceInput VfxInstanceInput;
struct VfxInstanceInput{
    size_t index;
    VfxInputType type;
    VfxInputValue initialValue;
    VfxAutomationKey* keys;
    VfxInstanceInput* next;
};

typedef struct VfxLayerSoundAutomationKey VfxLayerSoundAutomationKey;
struct VfxLayerSoundAutomationKey{
    VfxAutomationKeyType type;
    double len;
    double targetValue;
    VfxLayerSoundAutomationKey* next;
};

typedef struct{
    double initialValue;
    VfxLayerSoundAutomationKey* keys;
} VfxLayerSoundParameter;

typedef struct VfxInstance VfxInstance;
struct VfxInstance{
    size_t vfx_index;
    double offset;
    double duration;
    VfxInstanceInput* inputs;
    VfxInstance* next;
};

typedef struct Layer Layer;
struct Layer{
    MediaInstance* mediaInstances;
    Slice* slices;
    VfxInstance* vfxInstances;
    VfxLayerSoundParameter volume;
    VfxLayerSoundParameter pan;
    Layer* next;
};

typedef struct VfxDescriptor VfxDescriptor;
struct VfxDescriptor{
    const char* filename;
    VfxDescriptor* next;
};

typedef struct Project Project;
struct Project{
    Project_Settings settings;
    Layer* layers;
    VfxDescriptor* vfxDescriptors;
    ArenaAllocator* aa;
};

#endif