#include "myProject.h"
#include "human_readable_pointers.h"
#include "ffmpeg_helper.h"

#define NOB_STRIP_PREFIX
#include "nob.h"

#define FVFX_YES
#ifdef FVFX_YES
static void VfxInstance_Update(MyVfxs* myVfxs, VfxInstance* instance, double currentTime, void* push_constants_data) {
    for (size_t i = 0; i < instance->inputs.count; i++) {
        VfxInstanceInput* myInput = &instance->inputs.items[i];

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
                        lerpVfxValue(myInput->type, &result, &prevValue, &key->targetValue, t);
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
static void VfxInstance_Update(MyVfxs* myVfxs, VfxInstance* instance, double currentTime, void* push_constants_data) {
    for (size_t i = 0; i < instance->inputs.count; i++) {
        VfxInstanceInput* myInput = &instance->inputs.items[i];

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

static bool updateSlice(MyMedias* medias, Slices* slices, size_t currentSlice, size_t* currentMediaIndex,double* checkDuration){
    *currentMediaIndex = slices->items[currentSlice].media_index;
    *checkDuration = slices->items[currentSlice].duration;
    if(*currentMediaIndex == EMPTY_MEDIA) return true;
    MyMedia* media = &medias->items[*currentMediaIndex];
    if(*checkDuration == -1) *checkDuration = media->duration - slices->items[currentSlice].offset;
    ffmpegMediaSeek(&media->media, slices->items[currentSlice].offset);
    return true;
}

static int getVideoFrame(VkCommandBuffer cmd, Vulkanizer* vulkanizer, Project* project, Slices* slices, MyMedias* myMedias, VulkanizerVfxInstances* vulkanizerVfxInstances, Frame* frame, AVAudioFifo* audioFifo, GetVideoFrameArgs* args, VkImageView composedOutView){
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

static int getAudioFrame(Vulkanizer* vulkanizer, Project* project, Slices* slices, MyMedias* myMedias, Frame* frame, AVAudioFifo* audioFifo, GetVideoFrameArgs* args){
    assert(audioFifo);
    MyMedia* myMedia = &myMedias->items[args->currentMediaIndex];
    assert(myMedia->hasAudio && "You used wrong function!");
    assert(!myMedia->hasVideo && "You used wrong function!");

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

static int getImageFrame(VkCommandBuffer cmd, Vulkanizer* vulkanizer, Project* project, Slices* slices, MyMedias* myMedias, VulkanizerVfxInstances* vulkanizerVfxInstances, Frame* frame, GetVideoFrameArgs* args, VkImageView composedOutView){
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

static int getEmptyFrame(Vulkanizer* vulkanizer, Project* project, Slices* slices, MyMedias* myMedias, GetVideoFrameArgs* args){
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

static int getFrame(VkCommandBuffer cmd, Vulkanizer* vulkanizer, Project* project, Slices* slices, MyMedias* myMedias, VulkanizerVfxInstances* vulkanizerVfxInstances, Frame* frame, AVAudioFifo* audioFifo, GetVideoFrameArgs* args, VkImageView composedOutView){
    int e;
    while(true){
        if(args->currentMediaIndex == EMPTY_MEDIA){
            e = getEmptyFrame(vulkanizer,project,slices,myMedias,args);
        }else{
            MyMedia* myMedia = &myMedias->items[args->currentMediaIndex];
            if(myMedia->media.isImage) e = getImageFrame(cmd, vulkanizer,project,slices,myMedias,vulkanizerVfxInstances,frame,args,composedOutView);
            else if(myMedia->hasVideo) e = getVideoFrame(cmd, vulkanizer,project,slices,myMedias,vulkanizerVfxInstances,frame,audioFifo,args,composedOutView);
            else if(myMedia->hasAudio && !myMedia->hasVideo) e = getAudioFrame(vulkanizer,project,slices,myMedias,frame, audioFifo, args);
            else assert(false && "Unreachable");
        }

        if(e != -GET_FRAME_NEXT_MEDIA) return e;
    }
}

static int VfxInstanceInput_compare(const void* a, const void* b) {
    return ((VfxInstanceInput*)a)->index - ((VfxInstanceInput*)b)->index;
}

bool prepare_project(Project* project, Vulkanizer* vulkanizer, MyLayers* myLayers, MyVfxs* myVfxs, enum AVSampleFormat expectedSampleFormat, size_t fifo_size){
    for(size_t j = 0; j < project->layers.count; j++){
        Layer* layer = &project->layers.items[j];
        MyLayer myLayer = {0};
        bool hasAudio = false;
        for(size_t i = 0; i < layer->mediaInstances.count; i++){
            MyMedia myMedia = {0};
    
            // ffmpeg init
            if(!ffmpegMediaInit(layer->mediaInstances.items[i].filename, project->sampleRate, project->stereo, expectedSampleFormat, &myMedia.media)){
                fprintf(stderr, "Couldn't initialize ffmpeg media at %s!\n", layer->mediaInstances.items[i].filename);
                return false;
            }
    
            myMedia.duration = ffmpegMediaDuration(&myMedia.media);
            myMedia.hasAudio = myMedia.media.audioStream != NULL;
            myMedia.hasVideo = myMedia.media.videoStream != NULL;
            if(myMedia.hasAudio) hasAudio = true;
            
            if(myMedia.hasVideo){
                if(!Vulkanizer_init_image_for_media(vulkanizer, myMedia.media.videoCodecContext->width, myMedia.media.videoCodecContext->height, &myMedia.mediaImage, &myMedia.mediaImageMemory, &myMedia.mediaImageView, &myMedia.mediaImageStride, &myMedia.mediaDescriptorSet, &myMedia.mediaImageData)) return 1;
            }
            da_append(&myLayer.myMedias, myMedia);
        }
        if(hasAudio) myLayer.audioFifo = av_audio_fifo_alloc(expectedSampleFormat, project->stereo ? 2 : 1, fifo_size);
        da_append(myLayers, myLayer);
    }
    myLayers->fifo_fmt = expectedSampleFormat;
    myLayers->fifo_frame_size = fifo_size;
    myLayers->fifo_ch_layout = project->stereo ? (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO : (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;

    for(size_t i = 0; i < project->vfxDescriptors.count; i++){
        VulkanizerVfx vfx = {0};
        if(!Vulkanizer_init_vfx(vulkanizer, project->vfxDescriptors.items[i].filename, &vfx)) return false;
        da_append(myVfxs, vfx);
    }

    //creating rest of inputs needed + type checking
    for(size_t i = 0; i < project->layers.count; i++){
        Layer* layer = &project->layers.items[i];
        for(size_t j = 0; j < layer->vfxInstances.count; j++){
            VfxInstance* vfx = &layer->vfxInstances.items[j];
            assert(vfx->vfx_index < myVfxs->count);
            VulkanizerVfx* myVfx = &myVfxs->items[vfx->vfx_index];

            if(myVfx->module.inputs.count > 0){
                size_t originputsCount = vfx->inputs.count;
                for(size_t m = 0; m < myVfx->module.inputs.count; m++){
                    VfxInput* input = &myVfx->module.inputs.items[m];

                    bool needToAdd = true;
                    for(size_t n = 0; n < originputsCount; n++){
                        VfxInstanceInput* myInput = &vfx->inputs.items[n];
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
                    da_append(&vfx->inputs, ((VfxInstanceInput){
                        .index = m,
                        .type = input->type,
                        .initialValue = (input->defaultValue != NULL ? *input->defaultValue : (VfxInputValue){0}),
                    }));
                }

                qsort(vfx->inputs.items, vfx->inputs.count, sizeof(vfx->inputs.items[0]), VfxInstanceInput_compare);
            }else{
                if(vfx->inputs.count > 0){
                    fprintf(stderr, "layer %zu vfx instance %zu expected 0 inputs got %zu\n", i, j, vfx->inputs.count);
                    return false;
                }
            }
        }
    }

    return true;
}

bool init_my_project(Project* project, MyLayers* myLayers){
    for(size_t i = 0; i < myLayers->count; i++){
        MyLayer* myLayer = &myLayers->items[i];
        Layer* layer = &project->layers.items[i];
        if(!updateSlice(&myLayer->myMedias,&layer->slices, myLayer->args.currentSlice, &myLayer->args.currentMediaIndex, &myLayer->args.checkDuration)) return false;
        if(myLayer->myMedias.items[myLayer->args.currentMediaIndex].hasVideo) myLayer->args.lastVideoPts = layer->slices.items[myLayer->args.currentSlice].offset / av_q2d(myLayer->myMedias.items[myLayer->args.currentMediaIndex].media.videoStream->time_base);
        printf("[FVFX] Processing Layer %s Slice 1/%zu!\n", hrp_name(&myLayer->args), layer->slices.count);
    }
    return true;
}

int process_project(VkCommandBuffer cmd, Project* project, Vulkanizer* vulkanizer, MyLayers* myLayers, MyVfxs* myVfxs, VulkanizerVfxInstances* vulkanizerVfxInstances, double projectTime, void* push_constants_buf, VkImageView outComposedImageView, bool* enoughSamplesOUT){
    *enoughSamplesOUT = true;
    size_t finishedCount = 0;
    for(size_t i = 0; i < myLayers->count; i++){
        MyLayer* myLayer = &myLayers->items[i];
        if(myLayer->finished) {
            finishedCount++;
            continue;
        }
        Layer* layer = &project->layers.items[i];

        vulkanizerVfxInstances->count = 0;
        for(size_t j = 0; j < layer->vfxInstances.count; j++){
            VfxInstance* vfx = &layer->vfxInstances.items[j];
            if((vfx->duration != -1) && !(projectTime > vfx->offset && projectTime < vfx->offset + vfx->duration)) continue;

            if(vfx->inputs.count > 0) VfxInstance_Update(myVfxs, vfx, projectTime, push_constants_buf);
            da_append(vulkanizerVfxInstances, ((VulkanizerVfxInstance){.vfx = &myVfxs->items[vfx->vfx_index], .push_constants_data = push_constants_buf, .push_constants_size = myVfxs->items[vfx->vfx_index].module.pushContantsSize}));
        }

        int e = getFrame(cmd, vulkanizer, project, &layer->slices, &myLayer->myMedias, vulkanizerVfxInstances, &myLayer->frame, myLayer->audioFifo, &myLayer->args, outComposedImageView);
        
        if(myLayer->audioFifo && (myLayer->args.currentMediaIndex == EMPTY_MEDIA || (myLayer->args.currentMediaIndex != EMPTY_MEDIA && !myLayer->myMedias.items[myLayer->args.currentMediaIndex].hasAudio))){
            av_audio_fifo_add_silence(myLayer->audioFifo, myLayers->fifo_fmt, &myLayers->fifo_ch_layout, project->sampleRate / project->fps);
        }

        if(myLayer->audioFifo && myLayer->args.currentMediaIndex != EMPTY_MEDIA && av_audio_fifo_size(myLayer->audioFifo) < myLayers->fifo_frame_size) *enoughSamplesOUT = false;
        if(e == -GET_FRAME_ERR) return 1;
        if(e == -GET_FRAME_FINISHED) {printf("[FVFX] Layer %s finished\n", hrp_name(&myLayer->args));myLayer->finished = true; finishedCount++; continue;}
        if(e == -GET_FRAME_SKIP) continue;
    }
    return finishedCount == myLayers->count ? PROCESS_PROJECT_FINISHED : PROCESS_PROJECT_CONTINUE;
}