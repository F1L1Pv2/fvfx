#ifndef FVFX_PREVIEW
#define FVFX_PREVIEW

#include "project.h"
#include "arena_alloc.h"
int preview(Project* project, const char* project_filename, int argc, const char** argv, ArenaAllocator* aa);

#endif