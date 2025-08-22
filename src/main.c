#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "vulkanizer.h"
#include "ffmpeg_media.h"
#include "ffmpeg_media_render.h"

#define NOB_STRIP_PREFIX
#include "nob.h"

typedef struct{
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
    Slices slices;
} MediaInstance;

typedef struct{
    MediaInstance* items;
    size_t count;
    size_t capacity;
} MediaInstances;

typedef struct{
    const char* outputFilename;
    size_t width;
    size_t height;
    float fps;
    float bitrate;
    float sampleRate;
    bool hasAudio;
    bool stereo;
    MediaInstances mediaInstances;
} Project;

int main(){
    Project project = {0};
    project.outputFilename = "output.mp4";
    project.width = 1920;
    project.height = 1920;
    project.fps = 60.0f;
    project.bitrate = 0;
    project.sampleRate = 48000;
    project.hasAudio = true;
    project.stereo = true;

    {
        #define SLICER(offsetIN,durationIN) da_append(&slices,((Slice){.offset = (offsetIN), .duration = (durationIN)}))
        Slices slices = {0};
        SLICER(20.0, 2);
        SLICER(30.0, 5);
        SLICER(10.0, 2);
        SLICER(15.0, .5);
        SLICER(20.0, 2);
        SLICER(30.0, 5);
        SLICER(0.0, 1);
        SLICER(10.0, 2);
        SLICER(0.0, 1);
        SLICER(15.0, 5);
        #undef SLICER

        MediaInstance instance = {
            .filename = "D:\\videos\\tester.mp4",
            .slices = slices
        };

        da_append(&project.mediaInstances, instance);
    }

    Vulkanizer vulkanizer = {0};
    if(!Vulkanizer_init(&vulkanizer)) return 1;

    // ffmpeg init
    Media media = {0};
    //for now for simplicity im just hardcoding using one media
    Slices* slices = &project.mediaInstances.items[0].slices;
    if(!ffmpegMediaInit(project.mediaInstances.items[0].filename, &media)){
        fprintf(stderr, "Couldn't initialize ffmpeg media at %s!\n", project.mediaInstances.items[0].filename);
        return 1;
    }

    if(!Vulkanizer_init_images(&vulkanizer, media.videoCodecContext->width, media.videoCodecContext->height)) return 1;

    //init renderer
    MediaRenderContext renderContext = {0};
    if(!ffmpegMediaRenderInit(&media, project.outputFilename, &renderContext)){
        fprintf(stderr, "Couldn't initialize ffmpeg media renderer!\n");
        return 1;
    }
    
    double duration = ffmpegMediaDuration(&media);
    size_t currentSlice = 0;

    double checkDuration = slices->items[currentSlice].duration;
    if(checkDuration == -1) checkDuration = duration - slices->items[currentSlice].offset;

    double localTime = 0;
    double timeBase = 0;
    Frame frame = {0};

    ffmpegMediaSeek(&media, &frame, slices->items[currentSlice].offset);

    while(true){
        if(localTime >= checkDuration){
            currentSlice++;
            if(currentSlice >= slices->count) break;
            localTime = 0;
            timeBase+=checkDuration;
            checkDuration = slices->items[currentSlice].duration;
            if(checkDuration == -1) checkDuration = duration - slices->items[currentSlice].offset;
            ffmpegMediaSeek(&media, &frame, slices->items[currentSlice].offset);
        }

        if(!ffmpegMediaGetFrame(&media, &frame)) break;
        localTime = frame.frameTime - slices->items[currentSlice].offset;
        frame.frameTime = timeBase + localTime;

        if(frame.type == FRAME_TYPE_VIDEO){
            if(!Vulkanizer_apply_vfx_on_frame(&vulkanizer, &frame, &frame)) break;
        }

        ffmpegMediaRenderPassFrame(&renderContext, &frame);
    }

    ffmpegMediaRenderFinish(&renderContext);

    printf("Finished rendering!\n");

    return 0;
}