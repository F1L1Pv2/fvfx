#define NOB_STRIP_PREFIX
#include "nob.h"

#include "loader.h"
#include "engine/platform.h"

typedef bool (*project_init_type)(Project* project, int argc, const char** argv);

bool project_load(Project* project, const char* filename, int argc, const char** argv){
    void* dll = platform_load_dynamic_library(filename);
    if (dll == NULL) {
        fprintf(stderr, "Couldn't load project %s\n", filename);
        return false;
    }

    project_init_type project_init_ptr = (project_init_type)
        platform_load_dynamic_function(dll, "project_init");

    if (project_init_ptr == NULL) {
        fprintf(stderr, "Couldn't find project_init in %s\n", filename);
        platform_free_dynamic_library(dll);
        return false;
    }

    if (!project_init_ptr(project, argc, argv)) {
        platform_free_dynamic_library(dll);
        return false;
    }

    //TODO: i cannot free it because then it frees strings inside
    //TODO: so just dup the strings or do something else
    // platform_free_dynamic_library(dll);
    return true;
}