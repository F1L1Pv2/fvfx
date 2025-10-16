#include <stdio.h>

#include "project.h"
#include "loader.h"
#include "render.h"
#include "preview.h"
#include <string.h>
#include <assert.h>

enum {
    MODE_NONE = 0,
    MODE_RENDER,
    MODE_PREVIEW,
};

int main(int argc, const char** argv){
    const char* filename = argv[0];

    if(argc < 3){
        fprintf(stderr, "Usage: %s (render|preview) (project filepath) [aditional args for project]\n", filename);
        return 1;
    }

    // ------------------------------ project config code --------------------------------
    Project project = {0};
    const char* proj_filename = argv[2];
    size_t proj_argc = argc > 3 ? argc - 3 : 0;
    const char** proj_argv = argc > 3 ? argv+3 : NULL;
    if(!project_loader_load(&project, proj_filename, proj_argc, proj_argv)) {
        fprintf(stderr, "Couldn't load project\n");
        return 1;
    }

    // ------------------------------------------- editor code -----------------------------------------------------
    int mode = MODE_NONE;
    if(strcmp(argv[1], "render") == 0) mode = MODE_RENDER;
    else if(strcmp(argv[1], "preview") == 0) mode = MODE_PREVIEW;

    if(mode == MODE_NONE){
        fprintf(stderr, "Unknown mode %s please specify correct ones (render|preview)\n", argv[1]);
        return 1;
    }

    if(mode == MODE_RENDER){
        return render(&project);
    }else if(mode == MODE_PREVIEW){
        return preview(&project, proj_filename, proj_argc, proj_argv);
    }else assert(false && "UNREACHABLE");

    return 1;
}