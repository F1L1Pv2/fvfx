#ifndef FVFX_LOADER
#define FVFX_LOADER

#include "project.h"
#include <stdbool.h>

bool project_loader_load(Project* project, const char* filename, int argc, const char** argv);
void project_loader_clean(Project* project);

#endif