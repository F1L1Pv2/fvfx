#ifndef FVFX_SHADER_UTILS
#define FVFX_SHADER_UTILS

#define NOB_STRIP_PREFIX
#include "nob.h"

#include <stdint.h>

#include "project.h"

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

void shader_utils_reset_string_allocator();

#endif