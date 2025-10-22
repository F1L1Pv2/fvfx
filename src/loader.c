#define NOB_STRIP_PREFIX
#include "nob.h"

#include "loader.h"
#include "engine/platform.h"

typedef bool (*project_init_type)(Project* project, int argc, const char** argv);
typedef void (*project_clean_type)(Project* project);

bool project_loader_load(Project* project, const char* filename, int argc, const char** argv, ArenaAllocator* aa){
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

    //duping strings so they are not lost after unloading dll
    project->outputFilename = aa_strdup(aa, project->outputFilename);

    for(size_t i = 0; i < project->layers.count; i++){
        Layer* layer = &project->layers.items[i];
        for(size_t j = 0; j < layer->mediaInstances.count; j++){
            MediaInstance* media_instance = &layer->mediaInstances.items[j];
            media_instance->filename = aa_strdup(aa, media_instance->filename);
        }
    }

    for(size_t i = 0; i < project->vfxDescriptors.count; i++){
        VfxDescriptor* vfx_descriptor = &project->vfxDescriptors.items[i];
        vfx_descriptor->filename = aa_strdup(aa, vfx_descriptor->filename);
    }

    //cleaning up project (if it does any runtime allocation) in case of function not existing we just dont care
    project_clean_type project_clean_ptr = (project_clean_type)
        platform_load_dynamic_function(dll, "project_clean");
    if (project_clean_ptr != NULL) project_clean_ptr(project);

    platform_free_dynamic_library(dll);
    return true;
}

void project_loader_clean(Project* project, ArenaAllocator* aa){
    aa_reset(aa);
    Layers layers = project->layers;
    for(size_t i = 0; i < layers.count; i++){
        Layer* layer = &layers.items[i];
        if(layer->slices.items) free(layer->slices.items);
        if(layer->mediaInstances.items) free(layer->mediaInstances.items);

        for(size_t j = 0; j < layer->vfxInstances.count; j++){
            VfxInstance* vfx_instance = &layer->vfxInstances.items[j];
            for(size_t m = 0; m < vfx_instance->inputs.count; m++){
                VfxInstanceInput* vfx_instance_input = &vfx_instance->inputs.items[m];
                if(vfx_instance_input->keys.items) free(vfx_instance_input->keys.items);
            }
            if(vfx_instance->inputs.items) free(vfx_instance->inputs.items);
        }
        if(layer->vfxInstances.items) free(layer->vfxInstances.items);
    }
    layers.count = 0;

    VfxDescriptors vfxDescriptors = project->vfxDescriptors;
    vfxDescriptors.count = 0;

    *project = (Project){
        .layers = layers,
        .vfxDescriptors = vfxDescriptors,
    };

}