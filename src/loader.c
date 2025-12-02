#define NOB_STRIP_PREFIX
#include "nob.h"

#include "loader.h"
#include "engine/platform.h"
#include "ll.h"
#include "shader_utils.h"

static void* arena_alloc_func(size_t size, void* caller_data){
    return aa_alloc((ArenaAllocator*)caller_data, size);
}

typedef bool (*project_init_type)(Module* module, int argc, const char** argv);

void project_set_settings(Project* project, Project_Settings settings){
    project->settings = settings;
}

static String_Builder vfx_sb = {0};
size_t project_add_vfx(Project* project, const char* filename){
    VfxModule moduleIn = {.filepath = aa_strdup(project->aa, filename)};
    vfx_sb.count = 0;
    if(!read_entire_file(moduleIn.filepath,&vfx_sb)) return -1;
    if(!extractVFXModuleMetaData(nob_sb_to_sv(vfx_sb),&moduleIn, project->aa)) return -1;
    VfxModuleRef* module_ref = ll_push(&project->vfxModuleRefs, ((VfxModuleRef){.module = moduleIn}), arena_alloc_func, project->aa);
    VfxModule* module = &module_ref->module;

    ll_push(&project->vfxDescriptors, (VfxDescriptor){.module = module}, arena_alloc_func, project->aa);
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
        .filename = aa_strdup(project->aa, filename),
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
void vfx_instance_set_arg(Project* project, VfxInstance* vfx_instance, size_t input_index, VfxInputArg input_arg){
    VfxInstanceInput* exists = NULL;
    for(VfxInstanceInput* vfx_input = vfx_instance->inputs; vfx_input != NULL; vfx_input = vfx_input->next){
        if(vfx_input->index == input_index) {
            exists = vfx_input;
            break;
        }
    }
    if(exists == NULL) exists = ll_push(&vfx_instance->inputs, ((VfxInstanceInput){.index = input_index}), arena_alloc_func, project->aa);
    exists->initialValue = input_arg.value;
    exists->type = input_arg.type;
}
void vfx_instance_add_automation_key(Project* project, VfxInstance* vfx_instance, size_t input_index, VfxAutomationKeyType automation_key_type, double automation_duration, VfxInputArg target_arg){
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
        .targetValue = target_arg.value,
    }), arena_alloc_func, project->aa);
}

size_t vfx_get_input_index(Project* project, size_t vfx_index, const char* input_name){
    VfxModule* module = NULL;
    size_t vfxDescriptors_count = 0;
    for(VfxDescriptor* vfxDescriptor = project->vfxDescriptors; vfxDescriptor != NULL; vfxDescriptor = vfxDescriptor->next) vfxDescriptors_count++;
    if(vfx_index > vfxDescriptors_count) return -1;
    module = ((VfxDescriptor*)ll_at(project->vfxDescriptors, vfx_index))->module;
    if(module == NULL) return -1;
    size_t i = 0;
    for(VfxInput* input = module->inputs; input != NULL; input = input->next, i++){
        if(strcmp(input->name, input_name) == 0) return i;
    }
    return -1;
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
        .vfx_get_input_index = vfx_get_input_index,
    }, argc, argv)) {
        platform_free_dynamic_library(dll);
        return false;
    }

    //duping strings so they are not lost after unloading dll
    project->settings.outputFilename = aa_strdup(aa, project->settings.outputFilename);

    platform_free_dynamic_library(dll);
    return true;
}

void project_loader_clean(Project* project, ArenaAllocator* aa){
    aa_reset(aa);
    aa_reset(project->aa);
    *project = (Project){0};
}