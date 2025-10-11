#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "engine/vulkan_simple.h"
#include "vulkanizer.h"
#include "ffmpeg_media.h"
#include "ffmpeg_media_render.h"
#include "ffmpeg_helper.h"

#define HUMAN_READABLE_POINTERS_IMPLEMENTATION
#include "human_readable_pointers.h"

#define NOB_STRIP_PREFIX
#include "nob.h"

typedef struct{
    size_t media_index;
    double offset;
    double duration;
} Slice;

typedef struct{
    Slice* items;
    size_t count;
    size_t capacity;
} Slices;

typedef struct{
    const char* filename;
} MediaInstance;

typedef struct{
    MediaInstance* items;
    size_t count;
    size_t capacity;
} MediaInstances;

typedef enum{
    VFX_AUTO_KEY_LINEAR = 0,
    VFX_AUTO_KEY_STEP,
    VFX_AUTO_KEY_COUNT
} VfxAutomationKeyType;

typedef struct{
    VfxAutomationKeyType type;
    double len;
    VfxInputValue targetValue;
} VfxAutomationKey;

typedef struct{
    VfxAutomationKey* items;
    size_t count;
    size_t capacity;
} VfxAutomationKeys;

typedef struct{
    size_t index;
    VfxInputType type;
    VfxInputValue initialValue;
    VfxAutomationKeys keys;
} MyInput;

typedef struct{
    MyInput* items;
    size_t count;
    size_t capacity;
} MyInputs;

typedef struct{
    size_t vfx_index;
    double offset;
    double duration;
    MyInputs myInputs;
} VfxInstance;

typedef struct{
    VfxInstance* items;
    size_t count;
    size_t capacity;
} VfxInstances;

typedef struct{
    MediaInstances mediaInstances;
    Slices slices;
    VfxInstances vfxInstances;
} Layer;

typedef struct{
    Layer* items;
    size_t count;
    size_t capacity;
} Layers;

typedef struct{
    const char* filename;
} VfxDescriptor;

typedef struct{
    VfxDescriptor* items;
    size_t count; 
    size_t capacity;
} VfxDescriptors;

typedef struct{
    const char* outputFilename;
    size_t width;
    size_t height;
    float fps;
    float sampleRate;
    bool hasAudio;
    bool stereo;
    Layers layers;
    VfxDescriptors vfxDescriptors;
} Project;

#define EMPTY_MEDIA (-1)

typedef struct{
    Media media;
    bool hasAudio;
    bool hasVideo;

    VkImage mediaImage;
    VkDeviceMemory mediaImageMemory;
    VkImageView mediaImageView;
    size_t mediaImageStride;
    void* mediaImageData;
    VkDescriptorSet mediaDescriptorSet;
    double duration;
} MyMedia;

typedef struct{
    MyMedia* items;
    size_t count;
    size_t capacity;
} MyMedias;

typedef struct{
    VulkanizerVfx* items;
    size_t count;
    size_t capacity;
} MyVfxs;

#include <string.h>
#include <math.h>

#define LERP(a,b,t) ((a) + ((b) - (a)) * (t))

static void interpolateValue(VfxInputType type, VfxInputValue* out, VfxInputValue* a, VfxInputValue* b, double t) {
    switch (type) {
        case VFX_FLOAT:
            out->as.Float = (float)LERP(a->as.Float, b->as.Float, t);
            break;
        case VFX_DOUBLE:
            out->as.Double = LERP(a->as.Double, b->as.Double, t);
            break;
        case VFX_INT:
            out->as.Int = (int32_t)LERP(a->as.Int, b->as.Int, t);
            break;
        case VFX_UINT:
            out->as.Uint = (uint32_t)LERP(a->as.Uint, b->as.Uint, t);
            break;
        case VFX_VEC2:
            out->as.vec2.x = (float)LERP(a->as.vec2.x, b->as.vec2.x, t);
            out->as.vec2.y = (float)LERP(a->as.vec2.y, b->as.vec2.y, t);
            break;
        case VFX_VEC3:
            out->as.vec3.x = (float)LERP(a->as.vec3.x, b->as.vec3.x, t);
            out->as.vec3.y = (float)LERP(a->as.vec3.y, b->as.vec3.y, t);
            out->as.vec3.z = (float)LERP(a->as.vec3.z, b->as.vec3.z, t);
            break;
        case VFX_VEC4:
            out->as.vec4.x = (float)LERP(a->as.vec4.x, b->as.vec4.x, t);
            out->as.vec4.y = (float)LERP(a->as.vec4.y, b->as.vec4.y, t);
            out->as.vec4.z = (float)LERP(a->as.vec4.z, b->as.vec4.z, t);
            out->as.vec4.w = (float)LERP(a->as.vec4.w, b->as.vec4.w, t);
            break;
        default:
            *out = *a;
            break;
    }
}

#define FVFX_YES
#ifdef FVFX_YES
void VfxInstance_Update(MyVfxs* myVfxs, VfxInstance* instance, double currentTime, void* push_constants_data) {
    for (size_t i = 0; i < instance->myInputs.count; i++) {
        MyInput* myInput = &instance->myInputs.items[i];

        VfxInputValue result = myInput->initialValue;

        if (myInput->keys.count > 0) {
            double localTime = currentTime - instance->offset;
            double accumulated = 0.0;
            VfxInputValue prevValue = myInput->initialValue;
            int found = 0;

            for (size_t k = 0; k < myInput->keys.count; k++) {
                VfxAutomationKey* key = &myInput->keys.items[k];
                double keyStart = accumulated;
                double keyEnd = accumulated + key->len;

                if (localTime <= keyEnd) {
                    double t = (key->len > 0) ? (localTime - keyStart) / key->len : 1.0;
                    if (key->type == VFX_AUTO_KEY_STEP) {
                        result = key->targetValue;
                    } else {
                        interpolateValue(myInput->type, &result, &prevValue, &key->targetValue, t);
                    }
                    found = 1;
                    break;
                }

                prevValue = key->targetValue;
                accumulated = keyEnd;
            }

            // If time is after the last key, hold the last key’s value
            if (!found) {
                result = myInput->keys.items[myInput->keys.count - 1].targetValue;
            }
        }

        void* dst = (uint8_t*)push_constants_data +
            myVfxs->items[instance->vfx_index].module.inputs.items[myInput->index].push_constant_offset;

        memcpy(dst, &result.as, get_vfxInputTypeSize(myInput->type));
    }
}
#else
void VfxInstance_Update(MyVfxs* myVfxs, VfxInstance* instance, double currentTime, void* push_constants_data) {
    for (size_t i = 0; i < instance->myInputs.count; i++) {
        MyInput* myInput = &instance->myInputs.items[i];

        int shouldUpdate = 0;

        VfxInputValue result = myInput->initialValue;

        if (myInput->keys.count > 0) {
            double localTime = currentTime - instance->offset;
            double accumulated = 0.0;

            VfxInputValue prevValue = myInput->initialValue;
            for (size_t k = 0; k < myInput->keys.count; k++) {
                VfxAutomationKey* key = &myInput->keys.items[k];
                double keyStart = accumulated;
                double keyEnd = accumulated + key->len;

                if (localTime <= keyEnd) {
                    shouldUpdate = 1;
                    double t = (key->len > 0) ? (localTime - keyStart) / key->len : 1.0;
                    if (key->type == VFX_AUTO_KEY_STEP) {
                        result = key->targetValue;
                    } else {
                        interpolateValue(myInput->type, &result, &prevValue, &key->targetValue, t);
                    }
                    break;
                }

                prevValue = key->targetValue;
                accumulated = keyEnd;
            }
        }

        if (shouldUpdate) {
            void* dst = (uint8_t*)push_constants_data +
                myVfxs->items[instance->vfx_index].module.inputs.items[myInput->index].push_constant_offset;

            memcpy(dst, &result.as, get_vfxInputTypeSize(myInput->type));
        }
    }
}
#endif

static inline bool updateSlice(MyMedias* medias, Slices* slices, size_t currentSlice, size_t* currentMediaIndex,double* checkDuration){
    *currentMediaIndex = slices->items[currentSlice].media_index;
    *checkDuration = slices->items[currentSlice].duration;
    if(*currentMediaIndex == EMPTY_MEDIA) return true;
    MyMedia* media = &medias->items[*currentMediaIndex];
    if(*checkDuration == -1) *checkDuration = media->duration - slices->items[currentSlice].offset;
    ffmpegMediaSeek(&media->media, slices->items[currentSlice].offset);
    return true;
}

enum {
    GET_FRAME_ERR = 1,
    GET_FRAME_FINISHED,
    GET_FRAME_SKIP,
    GET_FRAME_NEXT_MEDIA,
};

typedef struct{
    double localTime;
    double checkDuration;
    size_t currentSlice;
    size_t currentMediaIndex;
    size_t video_skip_count;
    size_t times_to_catch_up_target_framerate;
    int64_t lastVideoPts;
} GetVideoFrameArgs;

typedef struct{
    MyMedias myMedias;
    AVAudioFifo* audioFifo;
    Frame frame;
    GetVideoFrameArgs args;
    bool finished;
} MyLayer;

typedef struct{
    MyLayer* items;
    size_t count;
    size_t capacity;
} MyLayers;

VkCommandBuffer cmd;

int getVideoFrame(Vulkanizer* vulkanizer, Project* project, Slices* slices, MyMedias* myMedias, VulkanizerVfxInstances* vulkanizerVfxInstances, Frame* frame, AVAudioFifo* audioFifo, GetVideoFrameArgs* args, VkImageView composedOutView){
    MyMedia* myMedia = &myMedias->items[args->currentMediaIndex];
    assert(myMedia->hasVideo && "You used wrong function!");
    while(true){
        if(args->localTime >= args->checkDuration){
            args->currentSlice++;
            if(args->currentSlice >= slices->count) return -GET_FRAME_FINISHED;
            printf("[FVFX] Processing Layer %s Slice %zu/%zu!\n", hrp_name(args),args->currentSlice+1, slices->count);
            args->localTime = 0;
            args->video_skip_count = 0;
            args->times_to_catch_up_target_framerate = 0;
            if(!updateSlice(myMedias,slices, args->currentSlice, &args->currentMediaIndex, &args->checkDuration)) return -GET_FRAME_ERR;
            if(myMedias->items[args->currentMediaIndex].hasVideo) args->lastVideoPts = slices->items[args->currentSlice].offset / av_q2d(myMedias->items[args->currentMediaIndex].media.videoStream->time_base);
            return -GET_FRAME_NEXT_MEDIA;
        }
    
        
        if(args->times_to_catch_up_target_framerate > 0){
            if(!Vulkanizer_apply_vfx_on_frame_and_compose(cmd, vulkanizer, vulkanizerVfxInstances, myMedia->mediaImageView, myMedia->mediaImageData, myMedia->mediaImageStride, myMedia->mediaDescriptorSet, frame, composedOutView)) return -GET_FRAME_ERR;
            args->times_to_catch_up_target_framerate--;
            return 0;
        }

        if(!ffmpegMediaGetFrame(&myMedia->media, frame)) {args->localTime = args->checkDuration; return -GET_FRAME_NEXT_MEDIA;};
        
        if(frame->type == FRAME_TYPE_VIDEO){
            args->localTime = frame->pts * av_q2d(myMedias->items[args->currentMediaIndex].media.videoStream->time_base)  - slices->items[args->currentSlice].offset;
            if(args->video_skip_count > 0){
                args->video_skip_count--;
                return -GET_FRAME_SKIP;
            }
    
            double framerate = 1.0 / ((double)(frame->pts - args->lastVideoPts) * av_q2d(myMedias->items[args->currentMediaIndex].media.videoStream->time_base));
            args->lastVideoPts = frame->pts;
    
            args->times_to_catch_up_target_framerate = 1;
            if(framerate < project->fps){
                args->times_to_catch_up_target_framerate = (size_t)(project->fps/framerate);
                if(args->times_to_catch_up_target_framerate == 0) args->times_to_catch_up_target_framerate = 1;
            }else if(framerate > project->fps){
                args->video_skip_count = (size_t)(framerate / project->fps);
            }
    
            if(!Vulkanizer_apply_vfx_on_frame_and_compose(cmd, vulkanizer, vulkanizerVfxInstances, myMedia->mediaImageView, myMedia->mediaImageData, myMedia->mediaImageStride, myMedia->mediaDescriptorSet, frame, composedOutView)) return -GET_FRAME_ERR;
            args->times_to_catch_up_target_framerate--;
            return 0;
        }else{
            args->localTime = frame->pts * av_q2d(myMedias->items[args->currentMediaIndex].media.audioStream->time_base)  - slices->items[args->currentSlice].offset;
            av_audio_fifo_write(audioFifo, (void**)frame->audio.data, frame->audio.nb_samples);
        }
    }

    return -GET_FRAME_ERR;
}

int getAudioFrame(Vulkanizer* vulkanizer, Project* project, Slices* slices, MyMedias* myMedias, Frame* frame, AVAudioFifo* audioFifo, GetVideoFrameArgs* args){
    assert(audioFifo);
    MyMedia* myMedia = &myMedias->items[args->currentMediaIndex];
    assert(myMedia->hasAudio && "You used wrong function!");
    assert(!myMedia->hasVideo && "You used wrong function!");
    size_t initialSize = av_audio_fifo_size(audioFifo);

    if(args->localTime < args->checkDuration){
        args->times_to_catch_up_target_framerate = slices->items[args->currentSlice].duration / (1/project->fps);
    }
    while(args->localTime < args->checkDuration){    

        if(!ffmpegMediaGetFrame(&myMedia->media, frame)) {args->localTime = args->checkDuration; return -GET_FRAME_NEXT_MEDIA;};
        assert(frame->type == FRAME_TYPE_AUDIO && "You fucked up");
        
        args->localTime = frame->pts * av_q2d(myMedias->items[args->currentMediaIndex].media.audioStream->time_base)  - slices->items[args->currentSlice].offset;
        av_audio_fifo_write(audioFifo, (void**)frame->audio.data, frame->audio.nb_samples);
    }

    if(args->times_to_catch_up_target_framerate > 0){
        args->times_to_catch_up_target_framerate--;
        return -GET_FRAME_SKIP;
    }

    args->currentSlice++;
    if(args->currentSlice >= slices->count) return -GET_FRAME_FINISHED;
    printf("[FVFX] Processing Layer %s Slice %zu/%zu!\n", hrp_name(args),args->currentSlice+1, slices->count);
    args->localTime = 0;
    args->video_skip_count = 0;
    args->times_to_catch_up_target_framerate = 0;
    if(!updateSlice(myMedias,slices, args->currentSlice, &args->currentMediaIndex, &args->checkDuration)) return -GET_FRAME_ERR;
    if(myMedias->items[args->currentMediaIndex].hasVideo) args->lastVideoPts = slices->items[args->currentSlice].offset / av_q2d(myMedias->items[args->currentMediaIndex].media.videoStream->time_base);
    return -GET_FRAME_NEXT_MEDIA;
}

int getImageFrame(Vulkanizer* vulkanizer, Project* project, Slices* slices, MyMedias* myMedias, VulkanizerVfxInstances* vulkanizerVfxInstances, Frame* frame, GetVideoFrameArgs* args, VkImageView composedOutView){
    if(args->localTime < args->checkDuration){
        args->times_to_catch_up_target_framerate = slices->items[args->currentSlice].duration / (1/project->fps);
        args->localTime = args->checkDuration;
    }

    if(args->times_to_catch_up_target_framerate > 0){
        MyMedia* myMedia = &myMedias->items[args->currentMediaIndex];
        args->times_to_catch_up_target_framerate--;
        if(!ffmpegMediaGetFrame(&myMedia->media, frame)) {args->localTime = args->checkDuration; return -GET_FRAME_NEXT_MEDIA;};
        assert(frame->type == FRAME_TYPE_VIDEO && "You used wrong function");
        if(!Vulkanizer_apply_vfx_on_frame_and_compose(cmd, vulkanizer, vulkanizerVfxInstances, myMedia->mediaImageView, myMedia->mediaImageData, myMedia->mediaImageStride, myMedia->mediaDescriptorSet, frame, composedOutView)) return -GET_FRAME_ERR;
        return 0;
    }

    args->currentSlice++;
    if(args->currentSlice >= slices->count) return -GET_FRAME_FINISHED;
    printf("[FVFX] Processing Layer %s Slice %zu/%zu!\n", hrp_name(args),args->currentSlice+1, slices->count);
    args->localTime = 0;
    args->video_skip_count = 0;
    args->times_to_catch_up_target_framerate = 0;
    if(!updateSlice(myMedias,slices, args->currentSlice, &args->currentMediaIndex, &args->checkDuration)) return -GET_FRAME_ERR;
    if(myMedias->items[args->currentMediaIndex].hasVideo) args->lastVideoPts = slices->items[args->currentSlice].offset / av_q2d(myMedias->items[args->currentMediaIndex].media.videoStream->time_base);
    return -GET_FRAME_NEXT_MEDIA;
}

int getEmptyFrame(Vulkanizer* vulkanizer, Project* project, Slices* slices, MyMedias* myMedias, GetVideoFrameArgs* args){
    if(args->localTime < args->checkDuration){
        args->times_to_catch_up_target_framerate = slices->items[args->currentSlice].duration / (1/project->fps);
        args->localTime = args->checkDuration;
    }

    if(args->times_to_catch_up_target_framerate > 0){
        args->times_to_catch_up_target_framerate--;
        return -GET_FRAME_SKIP;
    }

    args->currentSlice++;
    if(args->currentSlice >= slices->count) return -GET_FRAME_FINISHED;
    printf("[FVFX] Processing Layer %s Slice %zu/%zu!\n", hrp_name(args),args->currentSlice+1, slices->count);
    args->localTime = 0;
    args->video_skip_count = 0;
    args->times_to_catch_up_target_framerate = 0;
    if(!updateSlice(myMedias,slices, args->currentSlice, &args->currentMediaIndex, &args->checkDuration)) return -GET_FRAME_ERR;
    if(myMedias->items[args->currentMediaIndex].hasVideo) args->lastVideoPts = slices->items[args->currentSlice].offset / av_q2d(myMedias->items[args->currentMediaIndex].media.videoStream->time_base);
    return -GET_FRAME_NEXT_MEDIA;
}

int getFrame(Vulkanizer* vulkanizer, Project* project, Slices* slices, MyMedias* myMedias, VulkanizerVfxInstances* vulkanizerVfxInstances, Frame* frame, AVAudioFifo* audioFifo, GetVideoFrameArgs* args, VkImageView composedOutView){
    int e;
    while(true){
        if(args->currentMediaIndex == EMPTY_MEDIA){
            e = getEmptyFrame(vulkanizer,project,slices,myMedias,args);
        }else{
            MyMedia* myMedia = &myMedias->items[args->currentMediaIndex];
            if(myMedia->media.isImage) e = getImageFrame(vulkanizer,project,slices,myMedias,vulkanizerVfxInstances,frame,args,composedOutView);
            else if(myMedia->hasVideo) e = getVideoFrame(vulkanizer,project,slices,myMedias,vulkanizerVfxInstances,frame,audioFifo,args,composedOutView);
            else if(myMedia->hasAudio && !myMedia->hasVideo) e = getAudioFrame(vulkanizer,project,slices,myMedias,frame, audioFifo, args);
            else assert(false && "Unreachable");
        }

        if(e != -GET_FRAME_NEXT_MEDIA) return e;
    }
}

int MyInput_compare(const void* a, const void* b) {
    return ((MyInput*)a)->index - ((MyInput*)b)->index;
}

int main(){
    // ------------------------------ project config code --------------------------------
    Project project = {0};
    project.outputFilename = "output.mp4";
    project.width = 1920;
    project.height = 1080;
    project.fps = 60.0f;
    project.sampleRate = 48000;
    project.hasAudio = true;
    project.stereo = true;

    {
        Layer layer = {0};
        #define VFXO(filenameIN) da_append(&project.vfxDescriptors, ((VfxDescriptor){.filename = (filenameIN)}))
        #define LAYERO() do {da_append(&project.layers, layer); layer = (Layer){0};} while(0)
        #define MEDIER(filenameIN) da_append(&layer.mediaInstances, ((MediaInstance){.filename = (filenameIN)}))
        #define SLICER(mediaIndex, offsetIN,durationIN) da_append(&layer.slices,((Slice){.media_index = (mediaIndex),.offset = (offsetIN), .duration = (durationIN)}))
        #define EMPIER(durationIN) da_append(&layer.slices,((Slice){.media_index = EMPTY_MEDIA, .duration = (durationIN)}))
        #define VFXER(vfxIndex,offsetIN,durationIN) da_append(&layer.vfxInstances, ((VfxInstance){.vfx_index = (vfxIndex), .offset = (offsetIN), .duration = (durationIN)}))
        #define VFXER_ARG(INDEX,TYPE,INITIAL_VAL) da_append(&layer.vfxInstances.items[layer.vfxInstances.count-1].myInputs, ((MyInput){.index = (INDEX), .type = (TYPE), .initialValue = (INITIAL_VAL)}))
        #define VFXER_ARG_KEY(TYPE, LEN, VAL) da_append(&layer.vfxInstances.items[layer.vfxInstances.count-1].myInputs.items[layer.vfxInstances.items[layer.vfxInstances.count-1].myInputs.count-1].keys, ((VfxAutomationKey){.len = LEN, .type = TYPE, .targetValue = VAL}))

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

    // ------------------------------------------- editor code -----------------------------------------------------
    if(!vulkan_init_headless()) return 1;

    if(vkAllocateCommandBuffers(device,&(VkCommandBufferAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    },&cmd) != VK_SUCCESS) return 1;
        
    VkFence inFlightFence;
    if(vkCreateFence(device, &(VkFenceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    }, NULL, &inFlightFence) != VK_SUCCESS) return 1;

    Vulkanizer vulkanizer = {0};
    if(!Vulkanizer_init(device, descriptorPool, project.width, project.height, &vulkanizer)) return 1;

    //init renderer
    MediaRenderContext renderContext = {0};
    if(!ffmpegMediaRenderInit(project.outputFilename, project.width, project.height, project.fps, project.sampleRate, project.stereo, project.hasAudio, &renderContext)){
        fprintf(stderr, "Couldn't initialize ffmpeg media renderer!\n");
        return 1;
    }

    MyLayers myLayers = {0};

    for(size_t j = 0; j < project.layers.count; j++){
        Layer* layer = &project.layers.items[j];
        MyLayer myLayer = {0};
        bool hasAudio = false;
        for(size_t i = 0; i < layer->mediaInstances.count; i++){
            MyMedia myMedia = {0};
    
            // ffmpeg init
            if(!ffmpegMediaInit(layer->mediaInstances.items[i].filename, project.sampleRate, project.stereo, renderContext.audioCodecContext->sample_fmt, &myMedia.media)){
                fprintf(stderr, "Couldn't initialize ffmpeg media at %s!\n", layer->mediaInstances.items[i].filename);
                return 1;
            }
    
            myMedia.duration = ffmpegMediaDuration(&myMedia.media);
            myMedia.hasAudio = myMedia.media.audioStream != NULL;
            myMedia.hasVideo = myMedia.media.videoStream != NULL;
            if(myMedia.hasAudio) hasAudio = true;
            
            if(myMedia.hasVideo){
                if(!Vulkanizer_init_image_for_media(&vulkanizer, myMedia.media.videoCodecContext->width, myMedia.media.videoCodecContext->height, &myMedia.mediaImage, &myMedia.mediaImageMemory, &myMedia.mediaImageView, &myMedia.mediaImageStride, &myMedia.mediaDescriptorSet, &myMedia.mediaImageData)) return 1;
            }
            da_append(&myLayer.myMedias, myMedia);
        }
        if(hasAudio) myLayer.audioFifo = av_audio_fifo_alloc(renderContext.audioCodecContext->sample_fmt, project.stereo ? 2 : 1, renderContext.audioCodecContext->frame_size);;
        da_append(&myLayers, myLayer);
    }

    MyVfxs myVfxs = {0};
    for(size_t i = 0; i < project.vfxDescriptors.count; i++){
        VulkanizerVfx vfx = {0};
        if(!Vulkanizer_init_vfx(&vulkanizer, project.vfxDescriptors.items[i].filename, &vfx)) return 1;
        da_append(&myVfxs, vfx);
    }

    //creating rest of inputs needed + type checking
    for(size_t i = 0; i < project.layers.count; i++){
        Layer* layer = &project.layers.items[i];
        for(size_t j = 0; j < layer->vfxInstances.count; j++){
            VfxInstance* vfx = &layer->vfxInstances.items[j];
            assert(vfx->vfx_index < myVfxs.count);
            VulkanizerVfx* myVfx = &myVfxs.items[vfx->vfx_index];

            if(myVfx->module.inputs.count > 0){
                size_t origMyInputsCount = vfx->myInputs.count;
                for(size_t m = 0; m < myVfx->module.inputs.count; m++){
                    VfxInput* input = &myVfx->module.inputs.items[m];

                    bool needToAdd = true;
                    for(size_t n = 0; n < origMyInputsCount; n++){
                        MyInput* myInput = &vfx->myInputs.items[n];
                        if(myInput->index == m){
                            if(myInput->type != input->type){
                                fprintf(stderr, "layer %zu vfx instance %zu input %zu expected type %s got type %s\n", i, j, m, get_vfxInputTypeName(input->type), get_vfxInputTypeName(myInput->type));
                                return 1;
                            }
                            needToAdd = false;
                            break;
                        }
                    }

                    if(!needToAdd) continue;
                    da_append(&vfx->myInputs, ((MyInput){
                        .index = m,
                        .type = input->type,
                        .initialValue = (input->defaultValue != NULL ? *input->defaultValue : (VfxInputValue){0}),
                    }));
                }

                qsort(vfx->myInputs.items, vfx->myInputs.count, sizeof(vfx->myInputs.items[0]), MyInput_compare);
            }else{
                if(vfx->myInputs.count > 0){
                    fprintf(stderr, "layer %zu vfx instance %zu expected 0 inputs got %zu\n", i, j, vfx->myInputs.count);
                    return 1;
                }
            }
        }
    }
    
    RenderFrame renderFrame = {0};

    uint8_t** tempAudioBuf;
    int tempAudioBufLineSize;
    av_samples_alloc_array_and_samples(&tempAudioBuf,&tempAudioBufLineSize, project.stereo ? 2 : 1, renderContext.audioCodecContext->frame_size, renderContext.audioCodecContext->sample_fmt, 0);

    uint8_t** composedAudioBuf;
    int composedAudioBufLineSize;
    av_samples_alloc_array_and_samples(&composedAudioBuf,&composedAudioBufLineSize, project.stereo ? 2 : 1, renderContext.audioCodecContext->frame_size, renderContext.audioCodecContext->sample_fmt, 0);

    for(size_t i = 0; i < myLayers.count; i++){
        MyLayer* myLayer = &myLayers.items[i];
        Layer* layer = &project.layers.items[i];
        if(!updateSlice(&myLayer->myMedias,&layer->slices, myLayer->args.currentSlice, &myLayer->args.currentMediaIndex, &myLayer->args.checkDuration)) return 1;
        if(myLayer->myMedias.items[myLayer->args.currentMediaIndex].hasVideo) myLayer->args.lastVideoPts = layer->slices.items[myLayer->args.currentSlice].offset / av_q2d(myLayer->myMedias.items[myLayer->args.currentMediaIndex].media.videoStream->time_base);
        printf("[FVFX] Processing Layer %s Slice 1/%zu!\n", hrp_name(&myLayer->args), layer->slices.count);
    }

    double projectTime = 0.0;
    VulkanizerVfxInstances vulkanizerVfxInstances = {0};
    void* push_constants_buf = calloc(256, sizeof(uint8_t));

    VkImage outComposedImage;
    VkDeviceMemory outComposedImageMemory;
    VkImageView outComposedImageView;
    size_t outComposedImage_stride;
    void* outComposedImage_mapped;
    uint32_t* outComposedVideoFrame = malloc(project.width*project.height*sizeof(uint32_t));

    if(!createMyImage(&outComposedImage, 
        project.width, project.height, 
        &outComposedImageMemory, 
        &outComposedImageView, 
        &outComposedImage_stride, 
        &outComposedImage_mapped,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    )) return 1;

    VkCommandBuffer tempCmd = vkCmdBeginSingleTime();
    vkCmdTransitionImage(tempCmd, outComposedImage, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdEndSingleTime(tempCmd);

    while(true){
        vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &inFlightFence);
        
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo commandBufferBeginInfo = {0};
        commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBeginInfo.pNext = NULL;
        commandBufferBeginInfo.flags = 0;
        commandBufferBeginInfo.pInheritanceInfo = NULL;
        vkBeginCommandBuffer(cmd,&commandBufferBeginInfo);

        Vulkanizer_reset_pool();

        vkCmdTransitionImage(
            cmd,
            outComposedImage,
            VK_IMAGE_LAYOUT_GENERAL, 
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
            VK_PIPELINE_STAGE_TRANSFER_BIT, 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        );

        vkCmdBeginRenderingEX(cmd,
            .colorAttachment = outComposedImageView,
            .clearColor = COL_EMPTY,
            .renderArea = (
                (VkExtent2D){.width = project.width, .height= project.height}
            )
        );

        vkCmdEndRendering(cmd);

        size_t finishedCount = 0;
        bool enoughSamples = true;
        for(size_t i = 0; i < myLayers.count; i++){
            MyLayer* myLayer = &myLayers.items[i];
            if(myLayer->finished) {
                finishedCount++;
                continue;
            }
            Layer* layer = &project.layers.items[i];

            vulkanizerVfxInstances.count = 0;
            for(size_t j = 0; j < layer->vfxInstances.count; j++){
                VfxInstance* vfx = &layer->vfxInstances.items[j];
                if((vfx->duration != -1) && !(projectTime > vfx->offset && projectTime < vfx->offset + vfx->duration)) continue;

                if(vfx->myInputs.count > 0) VfxInstance_Update(&myVfxs, vfx, projectTime, push_constants_buf);
                da_append(&vulkanizerVfxInstances, ((VulkanizerVfxInstance){.vfx = &myVfxs.items[vfx->vfx_index], .push_constants_data = push_constants_buf, .push_constants_size = myVfxs.items[vfx->vfx_index].module.pushContantsSize}));
            }

            int e = getFrame(&vulkanizer, &project, &layer->slices, &myLayer->myMedias, &vulkanizerVfxInstances, &myLayer->frame, myLayer->audioFifo, &myLayer->args, outComposedImageView);
            
            if(myLayer->audioFifo && (myLayer->args.currentMediaIndex == EMPTY_MEDIA || (myLayer->args.currentMediaIndex != EMPTY_MEDIA && !myLayer->myMedias.items[myLayer->args.currentMediaIndex].hasAudio))){
                av_audio_fifo_add_silence(myLayer->audioFifo, renderContext.audioCodecContext->sample_fmt, &renderContext.audioCodecContext->ch_layout, project.sampleRate / project.fps);
            }

            if(myLayer->audioFifo && myLayer->args.currentMediaIndex != EMPTY_MEDIA && av_audio_fifo_size(myLayer->audioFifo) < renderContext.audioCodecContext->frame_size) enoughSamples = false;
            if(e == -GET_FRAME_ERR) return 1;
            if(e == -GET_FRAME_FINISHED) {printf("[FVFX] Layer %s finished\n", hrp_name(&myLayer->args));myLayer->finished = true; finishedCount++; continue;}
            if(e == -GET_FRAME_SKIP) continue;
        }
        if(finishedCount == myLayers.count) break;

        vkCmdTransitionImage(
            cmd,
            outComposedImage,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
            VK_IMAGE_LAYOUT_GENERAL, 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT
        );

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo = {0};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence);
        vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);

        for(size_t y = 0; y < project.height; y++){
            memcpy(
                ((uint8_t*)outComposedVideoFrame) + y*project.width*sizeof(uint32_t),
                ((uint8_t*)outComposedImage_mapped) + y*outComposedImage_stride,
                project.width*sizeof(uint32_t)
            );
        }

        renderFrame.type = RENDER_FRAME_TYPE_VIDEO;
        renderFrame.data = outComposedVideoFrame;
        renderFrame.size = project.width * project.height * sizeof(outComposedVideoFrame[0]);
        ffmpegMediaRenderPassFrame(&renderContext, &renderFrame);

        if(enoughSamples){
            av_samples_set_silence(composedAudioBuf, 0, renderContext.audioCodecContext->frame_size, project.stereo ? 2 : 1, renderContext.audioCodecContext->sample_fmt);
            for(size_t i = 0; i < myLayers.count; i++){
                MyLayer* myLayer = &myLayers.items[i];
                if(!myLayer->audioFifo) continue;
                int read = av_audio_fifo_read(myLayer->audioFifo, (void**)tempAudioBuf, renderContext.audioCodecContext->frame_size);
                bool conditionalMix = (myLayer->finished) || (myLayer->args.currentMediaIndex == EMPTY_MEDIA) || (myLayer->args.currentMediaIndex != EMPTY_MEDIA && !myLayer->myMedias.items[myLayer->args.currentMediaIndex].hasAudio);
                if(conditionalMix && read > 0){
                    mix_audio(composedAudioBuf, tempAudioBuf, read, project.stereo ? 2 : 1, renderContext.audioCodecContext->sample_fmt);
                    continue;
                }else if(conditionalMix && read == 0) continue;
                
                assert(read == renderContext.audioCodecContext->frame_size && "You fucked up smth my bruvskiers");
                mix_audio(composedAudioBuf, tempAudioBuf, read, project.stereo ? 2 : 1, renderContext.audioCodecContext->sample_fmt);
            }
            renderFrame.type = RENDER_FRAME_TYPE_AUDIO;
            renderFrame.data = composedAudioBuf;
            renderFrame.size = renderContext.audioCodecContext->frame_size;
            ffmpegMediaRenderPassFrame(&renderContext, &renderFrame);
        }

        projectTime += 1.0 / project.fps;
    }

    printf("[FVFX] Draining leftover audio\n");
    bool audioLeft = true;
    while (audioLeft) {
        audioLeft = false;
        for (size_t i = 0; i < myLayers.count; i++) {
            MyLayer* myLayer = &myLayers.items[i];
            if(!myLayer->audioFifo) continue;
            if (av_audio_fifo_size(myLayer->audioFifo) > 0) {
                audioLeft = true;
                break;
            }
        }
        if (!audioLeft) break;
        av_samples_set_silence(
            composedAudioBuf,
            0,
            renderContext.audioCodecContext->frame_size,
            project.stereo ? 2 : 1,
            renderContext.audioCodecContext->sample_fmt
        );
        for (size_t i = 0; i < myLayers.count; i++) {
            MyLayer* myLayer = &myLayers.items[i];
            if(!myLayer->audioFifo) continue;
            int available = av_audio_fifo_size(myLayer->audioFifo);
            if (available <= 0) continue;

            int toRead = FFMIN(available, renderContext.audioCodecContext->frame_size);
            int read = av_audio_fifo_read(
                myLayer->audioFifo,
                (void**)tempAudioBuf,
                toRead
            );
            mix_audio(
                composedAudioBuf,
                tempAudioBuf,
                read,
                project.stereo ? 2 : 1,
                renderContext.audioCodecContext->sample_fmt
            );
        }
        renderFrame.type = RENDER_FRAME_TYPE_AUDIO;
        renderFrame.data = composedAudioBuf;
        renderFrame.size = renderContext.audioCodecContext->frame_size;
        ffmpegMediaRenderPassFrame(&renderContext, &renderFrame);
    }

    ffmpegMediaRenderFinish(&renderContext);
    printf("[FVFX] Finished rendering!\n");

    return 0;
}