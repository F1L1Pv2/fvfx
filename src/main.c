#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

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

typedef struct{
    MediaInstances mediaInstances;
    Slices slices;
} Layer;

typedef struct{
    Layer* items;
    size_t count;
    size_t capacity;
} Layers;

typedef struct{
    const char* outputFilename;
    size_t width;
    size_t height;
    float fps;
    float sampleRate;
    bool hasAudio;
    bool stereo;
    Layers layers;
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
    double duration;
} MyMedia;

typedef struct{
    MyMedia* items;
    size_t count;
    size_t capacity;
} MyMedias;

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

int getVideoFrame(Vulkanizer* vulkanizer, Project* project, Slices* slices, MyMedias* myMedias, Frame* frame, AVAudioFifo* audioFifo, GetVideoFrameArgs* args, uint32_t* outVideoFrame){
    assert(audioFifo);
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
            if(!Vulkanizer_apply_vfx_on_frame(vulkanizer, myMedia->mediaImageView, myMedia->mediaImageData, myMedia->mediaImageStride, frame, outVideoFrame)) return -GET_FRAME_ERR;
            args->times_to_catch_up_target_framerate--;
            return 0;
        }

        if(!ffmpegMediaGetFrame(&myMedia->media, frame)) {args->localTime = args->checkDuration; return -GET_FRAME_SKIP;};
        
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
    
            if(!Vulkanizer_apply_vfx_on_frame(vulkanizer, myMedia->mediaImageView, myMedia->mediaImageData, myMedia->mediaImageStride, frame, outVideoFrame)) return -GET_FRAME_ERR;
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

        if(!ffmpegMediaGetFrame(&myMedia->media, frame)) {args->localTime = args->checkDuration; return -GET_FRAME_SKIP;};
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

int getFrame(Vulkanizer* vulkanizer, Project* project, Slices* slices, MyMedias* myMedias, Frame* frame, AVAudioFifo* audioFifo, GetVideoFrameArgs* args, uint32_t* outVideoFrame){
    int e;
    while(true){
        if(args->currentMediaIndex == EMPTY_MEDIA){
            e = getEmptyFrame(vulkanizer,project,slices,myMedias,args);
        }else{
            MyMedia* myMedia = &myMedias->items[args->currentMediaIndex];
            if(myMedia->hasVideo) e = getVideoFrame(vulkanizer,project,slices,myMedias,frame,audioFifo,args,outVideoFrame);
            else if(myMedia->hasAudio && !myMedia->hasVideo) e = getAudioFrame(vulkanizer,project,slices,myMedias,frame, audioFifo, args);
            else assert(false && "Unreachable");
        }

        if(e != -GET_FRAME_NEXT_MEDIA) return e;
    }
}

inline uint8_t blend_channel(uint8_t s, uint8_t d, uint8_t sa) {
    uint32_t t = s * sa + d * (255 - sa);
    return (t + 128) * 257 >> 16;
}

inline void composeImageBuffers(uint32_t* srcBuff, uint32_t* dstBuff, size_t width, size_t height){
    for (size_t j = 0; j < width * height; j++) {
        uint32_t src = srcBuff[j];
        uint32_t dst = dstBuff[j];

        uint8_t sr = (src >> 0)  & 0xFF;
        uint8_t sg = (src >> 8)  & 0xFF;
        uint8_t sb = (src >> 16) & 0xFF;
        uint8_t sa = (src >> 24) & 0xFF;

        uint8_t dr = (dst >> 0)  & 0xFF;
        uint8_t dg = (dst >> 8)  & 0xFF;
        uint8_t db = (dst >> 16) & 0xFF;
        uint8_t da = (dst >> 24) & 0xFF;

        uint8_t or_ = blend_channel(sr, dr, sa);
        uint8_t og  = blend_channel(sg, dg, sa);
        uint8_t ob  = blend_channel(sb, db, sa);

        uint32_t at = sa + da * (255 - sa);
        uint8_t oa = (at + 128) * 257 >> 16;

        dstBuff[j] =
            (oa << 24) | (ob << 16) | (og << 8) | (or_);
    }
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

    //TODO: add support for images
    {
        Layer layer = {0};
        #define LAYERO() do {da_append(&project.layers, layer); layer = (Layer){0};} while(0)
        #define MEDIER(filenameIN) da_append(&layer.mediaInstances, ((MediaInstance){.filename = filenameIN}))
        #define SLICER(mediaIndex, offsetIN,durationIN) da_append(&layer.slices,((Slice){.media_index = (mediaIndex),.offset = (offsetIN), .duration = (durationIN)}))
        #define EMPIER(durationIN) da_append(&layer.slices,((Slice){.media_index = EMPTY_MEDIA, .duration = (durationIN)}))

        MEDIER("D:\\videos\\gato.mp4");
        MEDIER("D:\\videos\\tester.mp4");
        SLICER(0, 0.0, -1);
        EMPIER(1.5);
        SLICER(1, 0.0, 10);
        LAYERO();

        MEDIER("D:\\videos\\gradient descentive incometrigger (remastered v3).mp4");
        MEDIER("D:\\sprzedam.flac");
        MEDIER("C:\\Users\\mlodz\\Downloads\\hop-on-minecraft(1).mp4");
        SLICER(1, 30.0, 4);
        SLICER(0, 30.0, 5);
        SLICER(2, 0.0, -1);
        LAYERO();
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
                if(!Vulkanizer_init_image_for_media(myMedia.media.videoCodecContext->width, myMedia.media.videoCodecContext->height, &myMedia.mediaImage, &myMedia.mediaImageMemory, &myMedia.mediaImageView, &myMedia.mediaImageStride, &myMedia.mediaImageData)) return 1;
            }
            da_append(&myLayer.myMedias, myMedia);
        }
        if(hasAudio) myLayer.audioFifo = av_audio_fifo_alloc(renderContext.audioCodecContext->sample_fmt, project.stereo ? 2 : 1, renderContext.audioCodecContext->frame_size);;
        da_append(&myLayers, myLayer);
    }
    
    RenderFrame renderFrame = {0};

    uint8_t** tempAudioBuf;
    int tempAudioBufLineSize;
    av_samples_alloc_array_and_samples(&tempAudioBuf,&tempAudioBufLineSize, project.stereo ? 2 : 1, renderContext.audioCodecContext->frame_size, renderContext.audioCodecContext->sample_fmt, 0);

    uint8_t** composedAudioBuf;
    int composedAudioBufLineSize;
    av_samples_alloc_array_and_samples(&composedAudioBuf,&composedAudioBufLineSize, project.stereo ? 2 : 1, renderContext.audioCodecContext->frame_size, renderContext.audioCodecContext->sample_fmt, 0);

    uint32_t* outVideoFrame = malloc(project.width*project.height*sizeof(uint32_t));
    uint32_t* outComposedVideoFrame = malloc(project.width*project.height*sizeof(uint32_t));

    for(size_t i = 0; i < myLayers.count; i++){
        MyLayer* myLayer = &myLayers.items[i];
        Layer* layer = &project.layers.items[i];
        if(!updateSlice(&myLayer->myMedias,&layer->slices, myLayer->args.currentSlice, &myLayer->args.currentMediaIndex, &myLayer->args.checkDuration)) return 1;
        if(myLayer->myMedias.items[myLayer->args.currentMediaIndex].hasVideo) myLayer->args.lastVideoPts = layer->slices.items[myLayer->args.currentSlice].offset / av_q2d(myLayer->myMedias.items[myLayer->args.currentMediaIndex].media.videoStream->time_base);
        printf("[FVFX] Processing Layer %s Slice 1/%zu!\n", hrp_name(&myLayer->args), layer->slices.count);
    }

    while(true){
        memset(outComposedVideoFrame, 0, project.width*project.height*sizeof(uint32_t));
        bool allFinished = true;
        bool enoughSamples = true;
        for(size_t i = 0; i < myLayers.count; i++){
            MyLayer* myLayer = &myLayers.items[i];
            if(myLayer->finished) continue;
            if(!myLayer->finished) allFinished = false;
            Layer* layer = &project.layers.items[i];

            int e = getFrame(&vulkanizer, &project, &layer->slices, &myLayer->myMedias, &myLayer->frame, myLayer->audioFifo, &myLayer->args, outVideoFrame);
            if(myLayer->args.currentMediaIndex != EMPTY_MEDIA && av_audio_fifo_size(myLayer->audioFifo) < renderContext.audioCodecContext->frame_size) enoughSamples = false;
            if(e == -GET_FRAME_ERR) return 1;
            if(e == -GET_FRAME_FINISHED) {printf("[FVFX] Layer %s finished\n", hrp_name(&myLayer->args));myLayer->finished = true; continue;}
            if(e == -GET_FRAME_SKIP) continue;
            composeImageBuffers(outVideoFrame, outComposedVideoFrame, project.width, project.height);
        }
        if(allFinished) break;

        renderFrame.type = RENDER_FRAME_TYPE_VIDEO;
        renderFrame.data = outComposedVideoFrame;
        renderFrame.size = project.width * project.height * sizeof(outComposedVideoFrame[0]);
        ffmpegMediaRenderPassFrame(&renderContext, &renderFrame);

        if(enoughSamples){
            av_samples_set_silence(composedAudioBuf, 0, renderContext.audioCodecContext->frame_size, project.stereo ? 2 : 1, renderContext.audioCodecContext->sample_fmt);
            for(size_t i = 0; i < myLayers.count; i++){
                MyLayer* myLayer = &myLayers.items[i];
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
    }

    printf("[FVFX] Draining leftover audio\n");
    bool audioLeft = true;
    while (audioLeft) {
        audioLeft = false;
        for (size_t i = 0; i < myLayers.count; i++) {
            MyLayer* myLayer = &myLayers.items[i];
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