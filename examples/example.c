#include "project_module.h"
#include <assert.h>

bool project_init(Module* module, int argc, const char** argv){
    (void)argc;
    (void)argv;
    
    module->project_set_settings(module->project, (Project_Settings){
        .outputFilename = "output.mp4",
        .width = 1920,
        .height = 1080,
        .fps = 60.0f,
        .sampleRate = 48000,
        .hasAudio = true,
        .stereo = true,
    });

    size_t vfx_fit_id = module->project_add_vfx(module->project, "./addons/fit.fvfx"); if(vfx_fit_id == -1) return false;
    size_t vfx_fish_eye_id = module->project_add_vfx(module->project, "./addons/fishEye.fvfx"); if(vfx_fish_eye_id == -1) return false;
    size_t vfx_translate_id = module->project_add_vfx(module->project, "./addons/translate.fvfx"); if(vfx_translate_id == -1) return false;
    size_t vfx_translate_Offset_index = module->vfx_get_input_index(module->project, vfx_translate_id, "offset"); if(vfx_translate_Offset_index == -1) return false;
    size_t vfx_coloring_id = module->project_add_vfx(module->project, "./addons/coloring.fvfx"); if(vfx_coloring_id == -1) return false;
    size_t vfx_coloring_Color_index = module->vfx_get_input_index(module->project, vfx_coloring_id, "color"); if(vfx_coloring_Color_index == -1) return false;

    {
        Layer* layer = module->project_create_and_add_layer(module->project, 1.0, 0.0);
        size_t gato_id = module->layer_add_media(module->project,layer, "D:\\videos\\gato.mp4");
        size_t tester_id = module->layer_add_media(module->project,layer, "D:\\videos\\tester.mp4");
        module->layer_add_slice(module->project,layer, gato_id, 0, -1);
        module->layer_add_empty(module->project,layer, 1.5);
        module->layer_add_slice(module->project,layer, tester_id, 0, 10);

        module->layer_create_and_add_vfx_instance(module->project,layer, vfx_fit_id, 0, -1);
        
        VfxInstance* coloring_orange = module->layer_create_and_add_vfx_instance(module->project,layer, vfx_coloring_id, 5, 10);
        module->vfx_instance_set_arg(module->project,coloring_orange, 0, (VfxInputArg){.type = VFX_VEC4, .value.as.vec4 = {1,0.5,0.2,1}});
    }

    {
        Layer* layer = module->project_create_and_add_layer(module->project, .1, -1);
        module->layer_add_pan_automation_key(module->project,layer,VFX_AUTO_KEY_LINEAR,.5,1);
        module->layer_add_pan_automation_key(module->project,layer,VFX_AUTO_KEY_LINEAR,.5,-1);
        module->layer_add_pan_automation_key(module->project,layer,VFX_AUTO_KEY_LINEAR,.5,1);
        module->layer_add_pan_automation_key(module->project,layer,VFX_AUTO_KEY_LINEAR,.5,-1);
        module->layer_add_pan_automation_key(module->project,layer,VFX_AUTO_KEY_LINEAR,.5,1);
        module->layer_add_pan_automation_key(module->project,layer,VFX_AUTO_KEY_LINEAR,.5,-1);
        module->layer_add_pan_automation_key(module->project,layer,VFX_AUTO_KEY_LINEAR,.5,0);

        module->layer_add_volume_automation_key(module->project,layer,VFX_AUTO_KEY_LINEAR, .5, .5);
        module->layer_add_volume_automation_key(module->project,layer,VFX_AUTO_KEY_LINEAR, .1, .05);
        module->layer_add_volume_automation_key(module->project,layer,VFX_AUTO_KEY_LINEAR, 2, .75);
        module->layer_add_volume_automation_key(module->project,layer,VFX_AUTO_KEY_STEP, .1, .1);

        size_t gradient_descentive_id = module->layer_add_media(module->project, layer, "D:\\videos\\gradient descentive incometrigger (remastered v3).mp4");
        size_t why_we_loose_id = module->layer_add_media(module->project,layer, "C:\\Users\\mlodz\\Downloads\\whywelose.mp3");
        size_t ballin_id = module->layer_add_media(module->project,layer, "D:\\videos\\IMG_2754.MOV");
        size_t jessie_id = module->layer_add_media(module->project,layer, "C:\\Users\\mlodz\\Downloads\\jessie.jpg");

        module->layer_add_slice(module->project,layer, why_we_loose_id, 90, 4);
        module->layer_add_slice(module->project,layer, gradient_descentive_id, 30, 5);
        module->layer_add_slice(module->project,layer, ballin_id, 0, -1);
        module->layer_add_slice(module->project,layer, ballin_id, 0, -1);
        module->layer_add_slice(module->project,layer, jessie_id, 0, 2.5);

        module->layer_create_and_add_vfx_instance(module->project,layer, vfx_fish_eye_id, 7, 5);
        module->layer_create_and_add_vfx_instance(module->project,layer, vfx_fit_id, 0, -1);

        {
            VfxInstance* translate = module->layer_create_and_add_vfx_instance(module->project,layer, vfx_translate_id, 5, 5);
            module->vfx_instance_set_arg(module->project,translate, 0, (VfxInputArg){.type = VFX_VEC2, .value.as.vec2 = {.x =  0.0, .y =  0.0}});
            module->vfx_instance_add_automation_key(module->project,translate, 0, VFX_AUTO_KEY_LINEAR, 1, (VfxInputArg){.type = VFX_VEC2, .value.as.vec2 = {.x =  0.5, .y =  0.5}});
        }
     
        {
            VfxInstance* translate = module->layer_create_and_add_vfx_instance(module->project,layer, vfx_translate_id, 20.6, 2);
            module->vfx_instance_set_arg(module->project,translate, 0,(VfxInputArg){.type = VFX_VEC2, .value.as.vec2 = {.x =  0.5, .y =  0.5}});
            module->vfx_instance_add_automation_key(module->project,translate, 0, VFX_AUTO_KEY_LINEAR, .5, (VfxInputArg){.type = VFX_VEC2, .value.as.vec2 = {.x = -0.5, .y =  0.5}});
            module->vfx_instance_add_automation_key(module->project,translate, 0, VFX_AUTO_KEY_LINEAR, .5, (VfxInputArg){.type = VFX_VEC2, .value.as.vec2 = {.x = -0.5, .y = -0.5}});
            module->vfx_instance_add_automation_key(module->project,translate, 0, VFX_AUTO_KEY_LINEAR, .5, (VfxInputArg){.type = VFX_VEC2, .value.as.vec2 = {.x =  0.5, .y =  0.5}});
        }
    }

    return true;
}