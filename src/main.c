#include <stdio.h>

#include "project.h"
#include "loader.h"
#include "render.h"
#include <string.h>
#include <assert.h>

enum {
    MODE_NONE = 0,
    MODE_RENDER,
    MODE_PREVIEW,
};

int main(int argc, char** argv){
    // ------------------------------ project config code --------------------------------
    Project project = {0};
    if(!project_load(&project, NULL)) {
        fprintf(stderr, "Couldn't load project\n");
        return 1;
    }

    // ------------------------------------------- editor code -----------------------------------------------------
    char* filename = argv[0];

    if(argc < 2){
        fprintf(stderr, "specify mode %s render|preview\n", filename);
        return 1;
    }

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
        assert(false && "implement this");
    }else assert(false && "UNREACHABLE");

    return 1;
}