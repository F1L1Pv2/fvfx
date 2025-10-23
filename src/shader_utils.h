#ifndef FVFX_SHADER_UTILS
#define FVFX_SHADER_UTILS

#define NOB_STRIP_PREFIX
#include "nob.h"

#include <stdint.h>

#include "project.h"
#include "arena_alloc.h"

typedef struct VfxInput VfxInput;

struct VfxInput{
    size_t push_constant_offset;
    VfxInputType type;
    const char* name;
    VfxInputValue* defaultValue;
    VfxInput* next;
};

typedef struct {
    const char* filepath;
    const char* name;
    const char* description;
    const char* author;
    VfxInput* inputs;
    size_t pushContantsSize;
    bool hasDefaultValues;
} VfxModule;

char* get_vfxInputTypeName(VfxInputType type);
size_t get_vfxInputTypeSize(VfxInputType type);
bool extractVFXModuleMetaData(String_View sv, VfxModule* out, ArenaAllocator* aa);
bool preprocessVFXModule(String_Builder* sb, VfxModule* module);

void lerpVfxValue(VfxInputType type, VfxInputValue* out, VfxInputValue* a, VfxInputValue* b, double t);

#endif