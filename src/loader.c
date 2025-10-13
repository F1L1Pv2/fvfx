#define NOB_STRIP_PREFIX
#include "nob.h"

#include "loader.h"

bool project_load(Project* project, const char* filename){
    project->outputFilename = "output.mp4";
    project->width = 1920;
    project->height = 1080;
    project->fps = 60.0f;
    project->sampleRate = 48000;
    project->hasAudio = true;
    project->stereo = true;

    {
        Layer layer = {0};
        #define VFXO(filenameIN) da_append(&project->vfxDescriptors, ((VfxDescriptor){.filename = (filenameIN)}))
        #define LAYERO() do {da_append(&project->layers, layer); layer = (Layer){0};} while(0)
        #define MEDIER(filenameIN) da_append(&layer.mediaInstances, ((MediaInstance){.filename = (filenameIN)}))
        #define SLICER(mediaIndex, offsetIN,durationIN) da_append(&layer.slices,((Slice){.media_index = (mediaIndex),.offset = (offsetIN), .duration = (durationIN)}))
        #define EMPIER(durationIN) da_append(&layer.slices,((Slice){.media_index = EMPTY_MEDIA, .duration = (durationIN)}))
        #define VFXER(vfxIndex,offsetIN,durationIN) da_append(&layer.vfxInstances, ((VfxInstance){.vfx_index = (vfxIndex), .offset = (offsetIN), .duration = (durationIN)}))
        #define VFXER_ARG(INDEX,TYPE,INITIAL_VAL) da_append(&layer.vfxInstances.items[layer.vfxInstances.count-1].inputs, ((VfxInstanceInput){.index = (INDEX), .type = (TYPE), .initialValue = (INITIAL_VAL)}))
        #define VFXER_ARG_KEY(TYPE, LEN, VAL) da_append(&layer.vfxInstances.items[layer.vfxInstances.count-1].inputs.items[layer.vfxInstances.items[layer.vfxInstances.count-1].inputs.count-1].keys, ((VfxAutomationKey){.len = LEN, .type = TYPE, .targetValue = VAL}))

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
            MEDIER("C:\\Users\\mlodz\\Downloads\\shrek.gif");
            MEDIER("C:\\Users\\mlodz\\Downloads\\jessie.jpg");
            SLICER(1, 60.0 + 30, 4);
            SLICER(0, 30.0, 5);
            SLICER(2, 0.0, -1);
            SLICER(2, 0.0, -1);
            SLICER(2, 0.0, -1);
            SLICER(2, 0.0, -1);
            SLICER(3, 0.0, 2);
    
            VFXER(1, 7, 5);
            
            VFXER(0, 0, -1);
    
            VFXER(2, 5, 5);
            VFXER_ARG(0, VFX_VEC2,                ((VfxInputValue){.as.vec2 = {.x =  0.0, .y =  0.0}}));
            VFXER_ARG_KEY(VFX_AUTO_KEY_LINEAR, 1, ((VfxInputValue){.as.vec2 = {.x =  0.5, .y =  0.5}}));
    
            VFXER(2, 18.1, 2);
            VFXER_ARG(0, VFX_VEC2,                 ((VfxInputValue){.as.vec2 = {.x =  0.5, .y =  0.5}}));
            VFXER_ARG_KEY(VFX_AUTO_KEY_LINEAR, .5, ((VfxInputValue){.as.vec2 = {.x = -0.5, .y =  0.5}}));
            VFXER_ARG_KEY(VFX_AUTO_KEY_LINEAR, .5, ((VfxInputValue){.as.vec2 = {.x = -0.5, .y = -0.5}}));
            VFXER_ARG_KEY(VFX_AUTO_KEY_LINEAR, .5, ((VfxInputValue){.as.vec2 = {.x =  0.5, .y =  0.5}}));
    
            LAYERO();
        }
    }

    return true;
}