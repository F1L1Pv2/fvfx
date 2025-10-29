#define NOB_STRIP_PREFIX
#include "nob.h"

#include "loader.h"
#include "engine/platform.h"
#include "ll.h"

static void* arena_alloc_func(size_t size, void* caller_data){
    return aa_alloc((ArenaAllocator*)caller_data, size);
}

typedef bool (*project_init_type)(Module* module, int argc, const char** argv);

void project_set_settings(Project* project, Project_Settings settings){
    project->settings = settings;
}
size_t project_add_vfx(Project* project, const char* filename){
    ll_push(&project->vfxDescriptors, (VfxDescriptor){.filename = filename}, arena_alloc_func, project->aa);
    size_t count = 0;
    for(VfxDescriptor* desc = project->vfxDescriptors; desc != NULL; desc = desc->next) count++;
    return count - 1;
}
Layer* project_create_and_add_layer(Project* project, double initial_volume, double initial_panning){
    Layer* out = ll_push(&project->layers, ((Layer){
        .volume = initial_volume,
        .pan = initial_panning,
    }), arena_alloc_func, project->aa);
    return out;
}
size_t layer_add_media(Project* project, Layer* layer, const char* filename){
    ll_push(&layer->mediaInstances, ((MediaInstance){
        .filename = filename,
    }), arena_alloc_func, project->aa);
    size_t count = 0;
    for(MediaInstance* media_instance = layer->mediaInstances; media_instance != NULL; media_instance = media_instance->next) count++;
    return count - 1;
}
void layer_add_slice(Project* project, Layer* layer, size_t media_index, double media_start_from, double slice_duration){
    ll_push(&layer->slices, ((Slice){
        .media_index = media_index,
        .offset = media_start_from,
        .duration = slice_duration,
    }), arena_alloc_func, project->aa);
}
void layer_add_empty(Project* project, Layer* layer, double empty_duration){
    ll_push(&layer->slices, ((Slice){
        .media_index = EMPTY_MEDIA,
        .duration = empty_duration,
    }), arena_alloc_func, project->aa);
}
VfxInstance* layer_create_and_add_vfx_instance(Project* project, Layer* layer, size_t vfx_index, double instance_when, double instance_duration){
    VfxInstance* out = ll_push(&layer->vfxInstances, ((VfxInstance){
        .vfx_index = vfx_index,
        .offset = instance_when,
        .duration = instance_duration,
    }), arena_alloc_func, project->aa);
    return out;
}
void layer_add_volume_automation_key(Project* project, Layer* layer, VfxAutomationKeyType automation_key_type, double automation_duration, double target_value){
    ll_push(&layer->volume.keys, ((VfxLayerSoundAutomationKey){
        .type = automation_key_type,
        .len = automation_duration,
        .targetValue = target_value,
    }), arena_alloc_func, project->aa);
}
void layer_add_pan_automation_key(Project* project, Layer* layer, VfxAutomationKeyType automation_key_type, double automation_duration, double target_value){
    ll_push(&layer->pan.keys, ((VfxLayerSoundAutomationKey){
        .type = automation_key_type,
        .len = automation_duration,
        .targetValue = target_value,
    }), arena_alloc_func, project->aa);
}
void vfx_instance_set_arg(Project* project, VfxInstance* vfx_instance, size_t input_index, VfxInputType input_type, VfxInputValue input_value){
    VfxInstanceInput* exists = NULL;
    for(VfxInstanceInput* vfx_input = vfx_instance->inputs; vfx_input != NULL; vfx_input = vfx_input->next){
        if(vfx_input->index == input_index) {
            exists = vfx_input;
            break;
        }
    }
    if(exists == NULL) exists = ll_push(&vfx_instance->inputs, ((VfxInstanceInput){.index = input_index}), arena_alloc_func, project->aa);
    exists->initialValue = input_value;
    exists->type = input_type;
}
void vfx_instance_add_automation_key(Project* project, VfxInstance* vfx_instance, size_t input_index, VfxAutomationKeyType automation_key_type, double automation_duration, VfxInputValue target_value){
    VfxInstanceInput* exists = NULL;
    for(VfxInstanceInput* vfx_input = vfx_instance->inputs; vfx_input != NULL; vfx_input = vfx_input->next){
        if(vfx_input->index == input_index) {
            exists = vfx_input;
            break;
        }
    }
    if(exists == NULL) return;
    ll_push(&exists->keys, ((VfxAutomationKey){
        .type = automation_key_type,
        .len = automation_duration,
        .targetValue = target_value,
    }), arena_alloc_func, project->aa);
}

bool project_loader_load(Project* project, const char* filename, int argc, const char** argv, ArenaAllocator* aa){
    project->aa = aa;

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

    if (!project_init_ptr(&(Module){
        .project = project,
        .project_set_settings = project_set_settings,
        .project_add_vfx = project_add_vfx,
        .project_create_and_add_layer = project_create_and_add_layer,
        .layer_add_media = layer_add_media,
        .layer_add_slice = layer_add_slice,
        .layer_add_empty = layer_add_empty,
        .layer_create_and_add_vfx_instance = layer_create_and_add_vfx_instance,
        .layer_add_volume_automation_key = layer_add_volume_automation_key,
        .layer_add_pan_automation_key = layer_add_pan_automation_key,
        .vfx_instance_set_arg = vfx_instance_set_arg,
        .vfx_instance_add_automation_key = vfx_instance_add_automation_key,
    }, argc, argv)) {
        platform_free_dynamic_library(dll);
        return false;
    }

    //duping strings so they are not lost after unloading dll
    project->settings.outputFilename = aa_strdup(aa, project->settings.outputFilename);

    for(Layer* layer = project->layers; layer != NULL; layer = layer->next){
        for(MediaInstance* media_instance = layer->mediaInstances; media_instance != NULL; media_instance = media_instance->next){
            media_instance->filename = aa_strdup(aa, media_instance->filename);
        }
    }

    for(VfxDescriptor* vfx_descriptor = project->vfxDescriptors; vfx_descriptor != NULL; vfx_descriptor = vfx_descriptor->next){
        vfx_descriptor->filename = aa_strdup(aa, vfx_descriptor->filename);
    }

    platform_free_dynamic_library(dll);
    return true;
}

void project_loader_clean(Project* project, ArenaAllocator* aa){
    aa_reset(aa);
    aa_reset(project->aa);
    *project = (Project){0};
}