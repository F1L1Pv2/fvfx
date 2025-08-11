#ifndef FVFX_VFX
#define FVFX_VFX

#define NOB_STRIP_PREFIX
#include "nob.h"

#include "vulkan/vulkan.h"
#include <stdbool.h>
#include <stdlib.h>
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
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    size_t pushContantsSize;
    void* defaultPushConstantValue;
} VfxModule;

typedef struct{
    VfxModule* module;
    void *inputPushConstants;
    bool opened;
} VfxInstance;

typedef struct{
    VfxInstance* items;
    size_t count;
    size_t capacity;
} VfxInstances;

typedef struct HashItem HashItem;

struct HashItem {
    HashItem* next;
    String_View key;
    VfxModule value;
};

typedef struct {
    HashItem** buckets;
    size_t bucket_count;
} Hashmap;

void initHashMap(Hashmap* hashmap, size_t size);
uint32_t hashFunction(const char *key, uint32_t map_size);
HashItem* getFromHashMap(Hashmap* hashmap, const char* key);
char* get_vfxInputTypeName(VfxInputType type);
size_t get_vfxInputTypeSize(VfxInputType type);
bool extractVFXModuleMetaData(String_View sv, VfxModule* out);
bool preprocessVFXModule(String_Builder* sb, VfxModule* module);

#endif