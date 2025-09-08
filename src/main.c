#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "vulkanizer.h"
#include "ffmpeg_media.h"
#include "ffmpeg_media_render.h"

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

typedef struct{
    const char* outputFilename;
    size_t width;
    size_t height;
    float fps;
    float sampleRate;
    bool hasAudio;
    bool stereo;
    MediaInstances mediaInstances;
    Slices slices;
} Project;

typedef struct{
    Media media;
    VkImage mediaImage;
    VkDeviceMemory mediaImageMemory;
    VkImageView mediaImageView;
    size_t mediaImageStride;
    void* mediaImageData;
    double duration;
} MyMedia;

typedef struct{
    MyMedia* items;
    size_t count;
    size_t capacity;
} MyMedias;

static inline bool updateSlice(Frame* frame, MyMedias* medias, Slices* slices, size_t currentSlice, size_t* currentMediaIndex,double* checkDuration){
    *currentMediaIndex = slices->items[currentSlice].media_index;
    MyMedia* media = &medias->items[*currentMediaIndex];
    *checkDuration = slices->items[currentSlice].duration;
    if(*checkDuration == -1) *checkDuration = media->duration - slices->items[currentSlice].offset;
    ffmpegMediaSeek(&media->media, frame, slices->items[currentSlice].offset);
    return true;
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
        #define MEDIER(filenameIN) da_append(&project.mediaInstances, ((MediaInstance){.filename = filenameIN}))
        MEDIER("D:\\videos\\tester.mp4");
        MEDIER("D:\\videos\\IMG_3590.mp4");
        MEDIER("D:\\videos\\IMG_3594.mp4");
        MEDIER("D:\\videos\\gato.mp4");
        MEDIER("D:\\videos\\gradient descentive incometrigger (remastered v3).mp4");
        #undef MEDIER

        #define SLICER(mediaIndex, offsetIN,durationIN) da_append(&project.slices,((Slice){.media_index = (mediaIndex),.offset = (offsetIN), .duration = (durationIN)}))
        SLICER(0, 20.0, 2);
        SLICER(1, 30.0, 5);
        SLICER(3, 0.0, 1);
        SLICER(1, 15.0, .5);
        SLICER(1, 20.0, 2);
        SLICER(2,0,-1);
        SLICER(0, 30.0, 5);
        SLICER(0, 0.0, 1);
        SLICER(4, 60.0, 5);
        SLICER(0, 0.0, 1);
        #undef SLICER
    }

    // ------------------------------------------- editor code -----------------------------------------------------

    //init renderer
    MediaRenderContext renderContext = {0};
    if(!ffmpegMediaRenderInit(project.outputFilename, project.width, project.height, project.fps, project.sampleRate, project.stereo, project.hasAudio, &renderContext)){
        fprintf(stderr, "Couldn't initialize ffmpeg media renderer!\n");
        return 1;
    }

    Vulkanizer vulkanizer = {0};
    if(!Vulkanizer_init(&vulkanizer)) return 1;
    if(!Vulkanizer_init_output_image(&vulkanizer, project.width, project.height)) return 1;

    MyMedias myMedias = {0};

    for(size_t i = 0; i < project.mediaInstances.count; i++){
        MyMedia myMedia = {0};

        // ffmpeg init
        if(!ffmpegMediaInit(project.mediaInstances.items[i].filename, project.sampleRate, project.stereo, renderContext.audioCodecContext->sample_fmt, &myMedia.media)){
            fprintf(stderr, "Couldn't initialize ffmpeg media at %s!\n", project.mediaInstances.items[i].filename);
            return 1;
        }

        myMedia.duration = ffmpegMediaDuration(&myMedia.media);
        
        if(!Vulkanizer_init_image_for_media(myMedia.media.videoCodecContext->width, myMedia.media.videoCodecContext->height, &myMedia.mediaImage, &myMedia.mediaImageMemory, &myMedia.mediaImageView, &myMedia.mediaImageStride, &myMedia.mediaImageData)) return 1;
        da_append(&myMedias, myMedia);
    }
    
    Frame frame = {0};

    size_t currentMediaIndex = -1;
    size_t currentSlice = 0;
    double localTime = 0;
    double checkDuration = 0;
    size_t video_skip_count = 0;
    if(!updateSlice(&frame, &myMedias,&project.slices, currentSlice, &currentMediaIndex, &checkDuration)) return 1;
    int64_t lastVideoPts = project.slices.items[currentSlice].offset / av_q2d(myMedias.items[currentMediaIndex].media.videoStream->time_base);

    RenderFrame renderFrame = {0};
    uint32_t* outVideoFrame = malloc(project.width*project.height*sizeof(uint32_t));
    printf("Processing Slice 1/%zu!\n", project.slices.count);
    while(true){
        MyMedia* myMedia = &myMedias.items[currentMediaIndex];
        while(localTime < checkDuration){
            if(!ffmpegMediaGetFrame(&myMedia->media, &frame)) break;
            
            if(frame.type == FRAME_TYPE_VIDEO){
                localTime = frame.pts * av_q2d(myMedias.items[currentMediaIndex].media.videoStream->time_base)  - project.slices.items[currentSlice].offset;
                if(video_skip_count > 0){
                    video_skip_count--;
                    continue;
                }

                double framerate = 1.0 / ((double)(frame.pts - lastVideoPts) * av_q2d(myMedias.items[currentMediaIndex].media.videoStream->time_base));
                lastVideoPts = frame.pts;

                size_t times_to_catch_up_target_framerate = 1;
                if(framerate < project.fps){
                    times_to_catch_up_target_framerate = (size_t)(project.fps/framerate);
                    if(times_to_catch_up_target_framerate == 0) times_to_catch_up_target_framerate = 1;
                }else if(framerate > project.fps){
                    video_skip_count = (size_t)(framerate / project.fps);
                }

                for(size_t i = 0; i < times_to_catch_up_target_framerate; i++){
                    if(!Vulkanizer_apply_vfx_on_frame(&vulkanizer, myMedia->mediaImageView, myMedia->mediaImageData, myMedia->mediaImageStride, &frame, outVideoFrame)) goto end;
                    renderFrame.type = RENDER_FRAME_TYPE_VIDEO;
                    renderFrame.data = outVideoFrame;
                    renderFrame.size = project.width * project.height * sizeof(outVideoFrame[0]);
                    ffmpegMediaRenderPassFrame(&renderContext, &renderFrame);
                }
            }else{
                localTime = frame.pts * av_q2d(myMedias.items[currentMediaIndex].media.audioStream->time_base)  - project.slices.items[currentSlice].offset;
                renderFrame.type = RENDER_FRAME_TYPE_AUDIO;
                renderFrame.data = frame.audio.data;
                renderFrame.size = frame.audio.nb_samples;
                ffmpegMediaRenderPassFrame(&renderContext, &renderFrame);
            }
        }

        currentSlice++;
        if(currentSlice >= project.slices.count) break;
        printf("Processing Slice %zu/%zu!\n", currentSlice+1, project.slices.count);
        localTime = 0;
        video_skip_count = 0;
        if(!updateSlice(&frame, &myMedias,&project.slices, currentSlice, &currentMediaIndex, &checkDuration)) goto end;
        lastVideoPts = project.slices.items[currentSlice].offset / av_q2d(myMedias.items[currentMediaIndex].media.videoStream->time_base);
    }

end:
    ffmpegMediaRenderFinish(&renderContext);

    printf("Finished rendering!\n");

    return 0;
}