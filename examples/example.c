#include "project.h"
#include <malloc.h>
#include <string.h>
#include <assert.h>

#define DA_REALLOC(optr, osize, new_size) realloc(optr, new_size)
#define da_reserve(da, extra) \
   do {\
      if((da)->count + extra >= (da)->capacity) {\
          void* _da_old_ptr;\
          size_t _da_old_capacity = (da)->capacity;\
          (void)_da_old_capacity;\
          (void)_da_old_ptr;\
          (da)->capacity = (da)->capacity*2+extra;\
          _da_old_ptr = (da)->items;\
          (da)->items = DA_REALLOC(_da_old_ptr, _da_old_capacity*sizeof(*(da)->items), (da)->capacity*sizeof(*(da)->items));\
          assert((da)->items && "Ran out of memory");\
      }\
   } while(0)
#define da_push(da, value) \
   do {\
        da_reserve(da, 1);\
        (da)->items[(da)->count++]=value;\
   } while(0)

static inline Layer blank_layer(){
    return (Layer){
        .volume.initialValue = 1.0,
    };
}

bool project_init(Project* project, int argc, const char** argv){
    (void)argc;
    (void)argv;
    
    project->outputFilename = "output.mp4";
    project->width = 1920;
    project->height = 1080;
    project->fps = 60.0f;
    project->sampleRate = 48000;
    project->hasAudio = true;
    project->stereo = true;

    {
        Layer layer = blank_layer();
        #define VFXO(filenameIN) da_push(&project->vfxDescriptors, ((VfxDescriptor){.filename = (filenameIN)}))
        #define LAYERO() do {da_push(&project->layers, layer); layer = blank_layer();} while(0)
        #define LAYERO_VOLUME(VAL) layer.volume.initialValue = (VAL)
        #define LAYERO_VOLUME_KEY(TYPE, LEN, VAL) da_push(&layer.volume.keys, ((VfxLayerSoundAutomationKey){.type = (TYPE), .len = (LEN), .targetValue = (VAL)}))
        #define LAYERO_PAN(VAL) layer.pan.initialValue = (VAL)
        #define LAYERO_PAN_KEY(TYPE, LEN, VAL) da_push(&layer.pan.keys, ((VfxLayerSoundAutomationKey){.type = (TYPE), .len = (LEN), .targetValue = (VAL)}))

        #define MEDIER(filenameIN) da_push(&layer.mediaInstances, ((MediaInstance){.filename = (filenameIN)}))
        #define SLICER(mediaIndex, offsetIN,durationIN) da_push(&layer.slices,((Slice){.media_index = (mediaIndex),.offset = (offsetIN), .duration = (durationIN)}))
        #define EMPIER(durationIN) da_push(&layer.slices,((Slice){.media_index = EMPTY_MEDIA, .duration = (durationIN)}))
        #define VFXER(vfxIndex,offsetIN,durationIN) da_push(&layer.vfxInstances, ((VfxInstance){.vfx_index = (vfxIndex), .offset = (offsetIN), .duration = (durationIN)}))
        #define VFXER_ARG(INDEX,TYPE,INITIAL_VAL) da_push(&layer.vfxInstances.items[layer.vfxInstances.count-1].inputs, ((VfxInstanceInput){.index = (INDEX), .type = (TYPE), .initialValue = (INITIAL_VAL)}))
        #define VFXER_ARG_KEY(TYPE, LEN, VAL) da_push(&layer.vfxInstances.items[layer.vfxInstances.count-1].inputs.items[layer.vfxInstances.items[layer.vfxInstances.count-1].inputs.count-1].keys, ((VfxAutomationKey){.len = LEN, .type = TYPE, .targetValue = VAL}))

        //global things
        VFXO("./addons/fit.fvfx");
        VFXO("./addons/fishEye.fvfx");
        VFXO("./addons/translate.fvfx");
        VFXO("./addons/coloring.fvfx");

        //per layer things
        { // layer 1
            MEDIER("D:\\videos\\gato.mp4");
            MEDIER("D:\\videos\\tester.mp4");
            SLICER(0, 0.0, -1);
            EMPIER(1.5);
            SLICER(1, 0.0, 10);
    
            VFXER(0, 0, -1);
    
            VFXER(3,5,10);
            VFXER_ARG(0,VFX_VEC4, ((VfxInputValue){.as.vec4 = {1,0.5,0.2,1}}));
            LAYERO();
        }

        { // layer 2
            MEDIER("D:\\videos\\gradient descentive incometrigger (remastered v3).mp4");
            MEDIER("C:\\Users\\mlodz\\Downloads\\whywelose.mp3");
            //TODO: gifs have wrong duration
            MEDIER("D:\\videos\\IMG_2754.MOV");
            MEDIER("C:\\Users\\mlodz\\Downloads\\jessie.jpg");
            SLICER(1, 60.0 + 30, 4);
            SLICER(0, 30.0, 5);
            SLICER(2, 0.0, -1);
            SLICER(2, 0.0, -1);
            SLICER(3, 0.0, 2.5);
    
            VFXER(1, 7, 5);
            
            VFXER(0, 0, -1);
    
            VFXER(2, 5, 5);
            VFXER_ARG(0, VFX_VEC2,                ((VfxInputValue){.as.vec2 = {.x =  0.0, .y =  0.0}}));
            VFXER_ARG_KEY(VFX_AUTO_KEY_LINEAR, 1, ((VfxInputValue){.as.vec2 = {.x =  0.5, .y =  0.5}}));
    
            VFXER(2, 20.6, 2);
            VFXER_ARG(0, VFX_VEC2,                 ((VfxInputValue){.as.vec2 = {.x =  0.5, .y =  0.5}}));
            VFXER_ARG_KEY(VFX_AUTO_KEY_LINEAR, .5, ((VfxInputValue){.as.vec2 = {.x = -0.5, .y =  0.5}}));
            VFXER_ARG_KEY(VFX_AUTO_KEY_LINEAR, .5, ((VfxInputValue){.as.vec2 = {.x = -0.5, .y = -0.5}}));
            VFXER_ARG_KEY(VFX_AUTO_KEY_LINEAR, .5, ((VfxInputValue){.as.vec2 = {.x =  0.5, .y =  0.5}}));
    
            LAYERO_PAN(-1);
            LAYERO_PAN_KEY(VFX_AUTO_KEY_LINEAR,.5,1);
            LAYERO_PAN_KEY(VFX_AUTO_KEY_LINEAR,.5,-1);
            LAYERO_PAN_KEY(VFX_AUTO_KEY_LINEAR,.5,1);
            LAYERO_PAN_KEY(VFX_AUTO_KEY_LINEAR,.5,-1);
            LAYERO_PAN_KEY(VFX_AUTO_KEY_LINEAR,.5,1);
            LAYERO_PAN_KEY(VFX_AUTO_KEY_LINEAR,.5,-1);
            LAYERO_PAN_KEY(VFX_AUTO_KEY_LINEAR,.5,0);

            LAYERO_VOLUME(.1);
            LAYERO_VOLUME_KEY(VFX_AUTO_KEY_LINEAR, .5, .5);
            LAYERO_VOLUME_KEY(VFX_AUTO_KEY_LINEAR, .1, .05);
            LAYERO_VOLUME_KEY(VFX_AUTO_KEY_LINEAR, 2, .75);
            LAYERO_VOLUME_KEY(VFX_AUTO_KEY_STEP, .1, .1);
            LAYERO();
        }
    }

    return true;
}