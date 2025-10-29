#define NOB_STRIP_PREFIX
#include "nob.h"

#include "loader.h"
#include "engine/platform.h"

typedef bool (*project_init_type)(Module* module, int argc, const char** argv);

void project_set_settings(Project* project, Project_Settings settings){
    project->settings = settings;
}
size_t project_add_vfx(Project* project, const char* filename){
    da_append(&project->vfxDescriptors, (VfxDescriptor){.filename = filename});
    return project->vfxDescriptors.count - 1;
}
Layer* project_create_and_add_layer(Project* project, double initial_volume, double initial_panning){
    da_append(&project->layers, ((Layer){
        .volume = initial_volume,
        .pan = initial_panning,
    }));
    return &project->layers.items[project->layers.count-1];
}
size_t layer_add_media(Layer* layer, const char* filename){
    da_append(&layer->mediaInstances, ((MediaInstance){
        .filename = filename,
    }));
    return layer->mediaInstances.count - 1;
}
void layer_add_slice(Layer* layer, size_t media_index, double media_start_from, double slice_duration){
    da_append(&layer->slices, ((Slice){
        .media_index = media_index,
        .offset = media_start_from,
        .duration = slice_duration,
    }));
}
void layer_add_empty(Layer* layer, double empty_duration){
    da_append(&layer->slices, ((Slice){
        .media_index = EMPTY_MEDIA,
        .duration = empty_duration,
    }));
}
VfxInstance* layer_create_and_add_vfx_instance(Layer* layer, size_t vfx_index, double instance_when, double instance_duration){
    da_append(&layer->vfxInstances, ((VfxInstance){
        .vfx_index = vfx_index,
        .offset = instance_when,
        .duration = instance_duration,
    }));
    return &layer->vfxInstances.items[layer->vfxInstances.count-1];
}
void layer_add_volume_automation_key(Layer* layer, VfxAutomationKeyType automation_key_type, double automation_duration, double target_value){
    da_append(&layer->volume.keys, ((VfxLayerSoundAutomationKey){
        .type = automation_key_type,
        .len = automation_duration,
        .targetValue = target_value,
    }));
}
void layer_add_pan_automation_key(Layer* layer, VfxAutomationKeyType automation_key_type, double automation_duration, double target_value){
    da_append(&layer->pan.keys, ((VfxLayerSoundAutomationKey){
        .type = automation_key_type,
        .len = automation_duration,
        .targetValue = target_value,
    }));
}
void vfx_instance_set_arg(VfxInstance* vfx_instance, size_t input_index, VfxInputType input_type, VfxInputValue input_value){
    VfxInstanceInput* exists = NULL;
    for(size_t i = 0; i < vfx_instance->inputs.count; i++){
        VfxInstanceInput* vfx_input = &vfx_instance->inputs.items[i];
        if(vfx_input->index == input_index) {
            exists = vfx_input;
            break;
        }
    }
    if(exists == NULL) {
        da_append(&vfx_instance->inputs, ((VfxInstanceInput){.index = input_index}));
        exists = &vfx_instance->inputs.items[vfx_instance->inputs.count-1];
    }
    exists->initialValue = input_value;
    exists->type = input_type;
}
void vfx_instance_add_automation_key(VfxInstance* vfx_instance, size_t input_index, VfxAutomationKeyType automation_key_type, double automation_duration, VfxInputValue target_value){
    VfxInstanceInput* exists = NULL;
    for(size_t i = 0; i < vfx_instance->inputs.count; i++){
        VfxInstanceInput* vfx_input = &vfx_instance->inputs.items[i];
        if(vfx_input->index == input_index) {
            exists = vfx_input;
            break;
        }
    }
    if(exists == NULL) return;
    da_append(&exists->keys, ((VfxAutomationKey){
        .type = automation_key_type,
        .len = automation_duration,
        .targetValue = target_value,
    }));
}

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

        if(layer->volume.keys.items) free(layer->volume.keys.items);
        if(layer->pan.keys.items) free(layer->pan.keys.items);
    }
    layers.count = 0;

    VfxDescriptors vfxDescriptors = project->vfxDescriptors;
    vfxDescriptors.count = 0;

    *project = (Project){
        .layers = layers,
        .vfxDescriptors = vfxDescriptors,
    };

}