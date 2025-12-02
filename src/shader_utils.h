#ifndef FVFX_SHADER_UTILS
#define FVFX_SHADER_UTILS

#define NOB_STRIP_PREFIX
#include "nob.h"

#include <stdint.h>

#include "project_module.h"
#include "arena_alloc.h"

char* get_vfxInputTypeName(VfxInputType type);
size_t get_vfxInputTypeSize(VfxInputType type);
bool extractVFXModuleMetaData(String_View sv, VfxModule* out, ArenaAllocator* aa);
bool preprocessVFXModule(String_Builder* sb, VfxModule* module);

void lerpVfxValue(VfxInputType type, VfxInputValue* out, VfxInputValue* a, VfxInputValue* b, double t);

#endif