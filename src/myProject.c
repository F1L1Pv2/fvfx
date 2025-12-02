#include "myProject.h"
#include "human_readable_pointers.h"
#include "ffmpeg_helper.h"

#include "ll.h"

static double VfxLayerSoundParameter_Evaluate(const VfxLayerSoundParameter* volume, double localTime) {
    double result = volume->initialValue;

    if (volume->keys == NULL)
        return result;

    double accumulated = 0.0;
    double prevValue = volume->initialValue;
    int found = 0;

    for(VfxLayerSoundAutomationKey* key = volume->keys; key != NULL; key = key->next) {
        double keyStart = accumulated;
        double keyEnd = accumulated + key->len;

        if (localTime <= keyEnd) {
            double t = (key->len > 0.0) ? (localTime - keyStart) / key->len : 1.0;

            if (key->type == VFX_AUTO_KEY_STEP) {
                result = key->targetValue;
            } else {
                result = prevValue + (key->targetValue - prevValue) * t;
            }

            found = 1;
            break;
        }

        prevValue = key->targetValue;
        accumulated = keyEnd;
    }

    if (!found) {
        size_t count = 0;
        for(VfxLayerSoundAutomationKey* key = volume->keys; key != NULL; key = key->next) count++;
        result = ((VfxLayerSoundAutomationKey*)ll_at(volume->keys, count-1))->targetValue;
    }

    return result;
}

static void VfxAutomation_Evaluate(
    VfxInputType type,
    const VfxInputValue* initialValue,
    VfxAutomationKey* keys,
    double localTime,
    VfxInputValue* outValue
) {
    *outValue = *initialValue;

    if (keys == NULL) return;

    double accumulated = 0.0;
    VfxInputValue prevValue = *initialValue;
    int found = 0;

    for(VfxAutomationKey* key = keys; key != NULL; key = key->next) {
        double keyStart = accumulated;
        double keyEnd = accumulated + key->len;

        if (localTime <= keyEnd) {
            double t = (key->len > 0) ? (localTime - keyStart) / key->len : 1.0;

            if (key->type == VFX_AUTO_KEY_STEP) {
                *outValue = key->targetValue;
            } else {
                lerpVfxValue(type, outValue, &prevValue, &key->targetValue, t);
            }

            found = 1;
            break;
        }

        prevValue = key->targetValue;
        accumulated = keyEnd;
    }

    if (!found) {
        size_t count = 0;
        for(VfxAutomationKey* key = keys; key != NULL; key = key->next) count++;
        *outValue = ((VfxAutomationKey*)ll_at(keys,count-1))->targetValue;
    }
}

static void VfxInstance_Update(MyVfx* myVfxs, VfxInstance* instance, double currentTime, void* push_constants_data) {
    for(VfxInstanceInput* myInput = instance->inputs; myInput != NULL; myInput = myInput->next){
        VfxInputValue result;

        double localTime = currentTime - instance->offset;
        VfxAutomation_Evaluate(myInput->type, &myInput->initialValue, myInput->keys, localTime, &result);

        MyVfx* myVfx = ll_at(myVfxs, instance->vfx_index);
        VfxInput* vfxInput = ll_at(myVfx->vfx.module->inputs, myInput->index);
        void* dst = (uint8_t*)push_constants_data + vfxInput->push_constant_offset;

        memcpy(dst, &result.as, get_vfxInputTypeSize(myInput->type));
    }
}

static bool updateSlice(MyMedia* medias, Slice* slices, size_t currentSlice, size_t* currentMediaIndex,double* checkDuration){
    *currentMediaIndex = ((Slice*)ll_at(slices,currentSlice))->media_index;
    *checkDuration = ((Slice*)ll_at(slices,currentSlice))->duration;
    if(*currentMediaIndex == EMPTY_MEDIA) return true;
    MyMedia* media = ll_at(medias,*currentMediaIndex);
    assert(checkDuration > 0 && "You fucked up");
    ffmpegMediaSeek(&media->media, ((Slice*)ll_at(slices,currentSlice))->offset);
    return true;
}

static int getVideoFrame(VkCommandBuffer cmd, Vulkanizer* vulkanizer, Project* project, Slice* slice, MyMedia* myMedias, VulkanizerVfxInstances* vulkanizerVfxInstances, Frame* frame, AVAudioFifo* audioFifo, GetVideoFrameArgs* args, VkImageView composedOutView){
    MyMedia* myMedia = ll_at(myMedias, args->currentMediaIndex);
    assert(myMedia->hasVideo && "You used wrong function!");
    while(true){
        if(args->localTime >= args->checkDuration) return -GET_FRAME_NEXT_MEDIA;
    
        
        if(args->times_to_catch_up_target_framerate > 0){
            if(!Vulkanizer_apply_vfx_on_frame_and_compose(cmd, vulkanizer, vulkanizerVfxInstances, myMedia->mediaImageView, myMedia->mediaImageData, myMedia->mediaImageStride, myMedia->mediaDescriptorSet, frame, composedOutView)) return -GET_FRAME_ERR;
            args->times_to_catch_up_target_framerate--;
            return 0;
        }

        if(!ffmpegMediaGetFrame(&myMedia->media, frame)) {args->localTime = args->checkDuration; return -GET_FRAME_NEXT_MEDIA;};
        
        if(frame->type == FRAME_TYPE_VIDEO){
            args->localTime = frame->pts * av_q2d(myMedia->media.videoStream->time_base)  - slice->offset;
            if(args->video_skip_count > 0){
                args->video_skip_count--;
                return -GET_FRAME_SKIP;
            }
    
            double framerate = 1.0 / ((double)(frame->pts - args->lastVideoPts) * av_q2d(myMedia->media.videoStream->time_base));
            args->lastVideoPts = frame->pts;
    
            args->times_to_catch_up_target_framerate = 1;
            if(framerate < project->settings.fps){
                args->times_to_catch_up_target_framerate = (size_t)(project->settings.fps/framerate);
                if(args->times_to_catch_up_target_framerate == 0) args->times_to_catch_up_target_framerate = 1;
            }else if(framerate > project->settings.fps){
                args->video_skip_count = (size_t)(framerate / project->settings.fps);
            }
    
            if(!Vulkanizer_apply_vfx_on_frame_and_compose(cmd, vulkanizer, vulkanizerVfxInstances, myMedia->mediaImageView, myMedia->mediaImageData, myMedia->mediaImageStride, myMedia->mediaDescriptorSet, frame, composedOutView)) return -GET_FRAME_ERR;
            args->times_to_catch_up_target_framerate--;
            return 0;
        }else{
            args->localTime = frame->pts * av_q2d(myMedia->media.audioStream->time_base)  - slice->offset;
            av_audio_fifo_write(audioFifo, (void**)frame->audio.data, frame->audio.nb_samples);
        }
    }

    return -GET_FRAME_ERR;
}

static int getAudioFrame(Vulkanizer* vulkanizer, Project* project, Slice* slice, MyMedia* myMedias, Frame* frame, AVAudioFifo* audioFifo, GetVideoFrameArgs* args){
    assert(audioFifo);
    MyMedia* myMedia = ll_at(myMedias, args->currentMediaIndex);
    assert(myMedia->hasAudio && "You used wrong function!");
    assert(!myMedia->hasVideo && "You used wrong function!");

    if(args->localTime < args->checkDuration){
        args->times_to_catch_up_target_framerate = (slice->duration - args->localTime) / (1/project->settings.fps);
    }
    while(args->localTime < args->checkDuration){    

        if(!ffmpegMediaGetFrame(&myMedia->media, frame)) {args->localTime = args->checkDuration; return -GET_FRAME_NEXT_MEDIA;};
        assert(frame->type == FRAME_TYPE_AUDIO && "You fucked up");
        
        args->localTime = frame->pts * av_q2d(myMedia->media.audioStream->time_base)  - slice->offset;
        av_audio_fifo_write(audioFifo, (void**)frame->audio.data, frame->audio.nb_samples);
    }

    if(args->times_to_catch_up_target_framerate > 0){
        args->times_to_catch_up_target_framerate--;
        return -GET_FRAME_SKIP;
    }

    return -GET_FRAME_NEXT_MEDIA;
}

static int getImageFrame(VkCommandBuffer cmd, Vulkanizer* vulkanizer, Project* project, Slice* slice, MyMedia* myMedias, VulkanizerVfxInstances* vulkanizerVfxInstances, Frame* frame, GetVideoFrameArgs* args, VkImageView composedOutView){
    if(args->localTime < args->checkDuration){
        args->times_to_catch_up_target_framerate = (slice->duration - args->localTime) / (1/project->settings.fps);
        args->localTime = args->checkDuration;
    }

    if(args->times_to_catch_up_target_framerate > 0){
        MyMedia* myMedia = ll_at(myMedias, args->currentMediaIndex);
        args->times_to_catch_up_target_framerate--;
        if(!ffmpegMediaGetFrame(&myMedia->media, frame)) {args->localTime = args->checkDuration; return -GET_FRAME_NEXT_MEDIA;};
        assert(frame->type == FRAME_TYPE_VIDEO && "You used wrong function");
        if(!Vulkanizer_apply_vfx_on_frame_and_compose(cmd, vulkanizer, vulkanizerVfxInstances, myMedia->mediaImageView, myMedia->mediaImageData, myMedia->mediaImageStride, myMedia->mediaDescriptorSet, frame, composedOutView)) return -GET_FRAME_ERR;
        return 0;
    }

    return -GET_FRAME_NEXT_MEDIA;
}

static int getEmptyFrame(Vulkanizer* vulkanizer, Project* project, Slice* slice, MyMedia* myMedias, GetVideoFrameArgs* args){
    if(args->localTime < args->checkDuration){
        args->times_to_catch_up_target_framerate = (slice->duration - args->localTime) / (1/project->settings.fps);
        args->localTime = args->checkDuration;
    }

    if(args->times_to_catch_up_target_framerate > 0){
        args->times_to_catch_up_target_framerate--;
        return -GET_FRAME_SKIP;
    }

    return -GET_FRAME_NEXT_MEDIA;
}

static int getFrame(VkCommandBuffer cmd, Vulkanizer* vulkanizer, Project* project, Slice* slices, MyMedia* myMedias, VulkanizerVfxInstances* vulkanizerVfxInstances, Frame* frame, AVAudioFifo* audioFifo, GetVideoFrameArgs* args, VkImageView composedOutView){
    int e;
    MyMedia* myMedia = ll_at(myMedias, args->currentMediaIndex);
    Slice* current_slice = ll_at(slices, args->currentSlice);

    while(true){
        if(current_slice == NULL) return -GET_FRAME_FINISHED;

        if(args->currentMediaIndex == EMPTY_MEDIA){
            e = getEmptyFrame(vulkanizer,project,current_slice,myMedias,args);
        }else{
            if(myMedia->media.isImage) e = getImageFrame(cmd, vulkanizer,project,current_slice,myMedias,vulkanizerVfxInstances,frame,args,composedOutView);
            else if(myMedia->hasVideo) e = getVideoFrame(cmd, vulkanizer,project,current_slice,myMedias,vulkanizerVfxInstances,frame,audioFifo,args,composedOutView);
            else if(myMedia->hasAudio && !myMedia->hasVideo) e = getAudioFrame(vulkanizer,project,current_slice,myMedias,frame, audioFifo, args);
            else assert(false && "Unreachable");
        }

        if(e == -GET_FRAME_NEXT_MEDIA){
            args->currentSlice++;
            current_slice = ll_at(slices, args->currentSlice);
            if(current_slice == NULL) return -GET_FRAME_FINISHED;
            printf("[FVFX] Processing Layer %s Slice %zu!\n", hrp_name(args),args->currentSlice+1);
            args->localTime = 0;
            args->video_skip_count = 0;
            args->times_to_catch_up_target_framerate = 0;
            if(!updateSlice(myMedias,slices, args->currentSlice, &args->currentMediaIndex, &args->checkDuration)) return -GET_FRAME_ERR;
            if(args->currentMediaIndex == EMPTY_MEDIA) continue;
            myMedia = ll_at(myMedias, args->currentMediaIndex);
            if(myMedia->hasVideo) args->lastVideoPts = current_slice->offset / av_q2d(myMedia->media.videoStream->time_base);
            continue;
        }
        return e;
    }
}

static int VfxInstanceInput_compare(const void* a, const void* b) {
    return ((VfxInstanceInput*)a)->index - ((VfxInstanceInput*)b)->index;
}

static void* ll_arena_allocator(size_t size, void* caller_data){
    return aa_alloc((ArenaAllocator*)caller_data,size);
}

bool prepare_project(Project* project, MyProject* myProject, Vulkanizer* vulkanizer, enum AVSampleFormat expectedSampleFormat, size_t fifo_size, ArenaAllocator* aa){
    for(Layer* layer = project->layers; layer != NULL; layer = layer->next){
        MyLayer myLayer = {0};
        myLayer.volume = layer->volume.initialValue;
        myLayer.pan = layer->pan.initialValue;
        bool hasAudio = false;
        for(MediaInstance* mediaInstance = layer->mediaInstances; mediaInstance != NULL; mediaInstance = mediaInstance->next){
            MyMedia myMedia = {0};
    
            // ffmpeg init
            if(!ffmpegMediaInit(mediaInstance->filename, project->settings.sampleRate, project->settings.stereo, expectedSampleFormat, &myMedia.media)){
                fprintf(stderr, "Couldn't initialize ffmpeg media at %s!\n", mediaInstance->filename);
                return false;
            }
    
            myMedia.duration = ffmpegMediaDuration(&myMedia.media);
            myMedia.hasAudio = myMedia.media.audioStream != NULL;
            myMedia.hasVideo = myMedia.media.videoStream != NULL;
            if(myMedia.hasAudio) hasAudio = true;
            
            if(myMedia.hasVideo){
                if(!Vulkanizer_init_image_for_media(vulkanizer, myMedia.media.videoCodecContext->width, myMedia.media.videoCodecContext->height, &myMedia.mediaImage, &myMedia.mediaImageMemory, &myMedia.mediaImageView, &myMedia.mediaImageStride, &myMedia.mediaDescriptorSet, &myMedia.mediaImageData)) return false;
            }
            ll_push(&myLayer.myMedias, myMedia, ll_arena_allocator, aa);
        }
        if(hasAudio) myLayer.audioFifo = av_audio_fifo_alloc(expectedSampleFormat, project->settings.stereo ? 2 : 1, fifo_size);
        ll_push(&myProject->myLayers, myLayer, ll_arena_allocator, aa);
    }
    myProject->myLayers_fifo_fmt = expectedSampleFormat;
    myProject->myLayers_fifo_frame_size = fifo_size;
    myProject->myLayers_fifo_ch_layout = project->settings.stereo ? (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO : (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;

    size_t vfx_descriptors_count = 0;
    for(VfxDescriptor* vfx_descriptor = project->vfxDescriptors; vfx_descriptor != NULL; vfx_descriptor = vfx_descriptor->next){
        MyVfx vfx = {0};
        if(!Vulkanizer_init_vfx(vulkanizer, vfx_descriptor->module, &vfx.vfx)) return false;
        ll_push(&myProject->myVfxs, vfx, ll_arena_allocator, aa);
        vfx_descriptors_count++;
    }

    //creating rest of inputs needed + type checking
    for(Layer* layer = project->layers; layer != NULL; layer = layer->next){
        size_t layer_id = 0;
        for(VfxInstance* vfx = layer->vfxInstances; vfx != NULL; vfx = vfx->next){
            size_t vfx_instance_id = 0;
            assert(vfx->vfx_index < vfx_descriptors_count);
            MyVfx* myVfx_ref = ll_at(myProject->myVfxs,vfx->vfx_index);
            VulkanizerVfx* myVfx = &myVfx_ref->vfx;
            
            if(myVfx->module->inputs != NULL){
                size_t originputsCount = 0;
                for(VfxInstanceInput* input = vfx->inputs; input != NULL; input = input->next) originputsCount++;
                size_t m = 0;
                for(VfxInput* input = myVfx->module->inputs; input != NULL; input = input->next,m++){
                    bool needToAdd = true;
                    for(size_t n = 0; n < originputsCount; n++){
                        VfxInstanceInput* myInput = ll_at(vfx->inputs,n);
                        if(myInput->index >= originputsCount){
                            fprintf(stderr, "layer %zu vfx instance %zu input %zu doesn't exist\n", layer_id, vfx_instance_id, m);
                            return false;
                        }
                        if(myInput->index == m){
                            if(myInput->type != input->type){
                                fprintf(stderr, "layer %zu vfx instance %zu input %zu expected type %s got type %s\n", layer_id, vfx_instance_id, m, get_vfxInputTypeName(input->type), get_vfxInputTypeName(myInput->type));
                                return false;
                            }
                            needToAdd = false;
                            break;
                        }
                    }

                    if(!needToAdd) continue;
                    ll_push(&vfx->inputs, ((VfxInstanceInput){
                        .index = m,
                        .type = input->type,
                        .initialValue = (input->defaultValue != NULL ? *input->defaultValue : (VfxInputValue){0}),
                    }), ll_arena_allocator, aa);
                }

                ll_qsort(&vfx->inputs, VfxInstanceInput_compare);
            }else{
                if(vfx->inputs != NULL){
                    size_t originputsCount = 0;
                    for(VfxInstanceInput* input = vfx->inputs; input != NULL; input = input->next) originputsCount++;
                    fprintf(stderr, "layer %zu vfx instance %zu expected 0 inputs got %zu\n", layer_id, vfx_instance_id, originputsCount);
                    return false;
                }
            }
            vfx_instance_id++;
        }
        layer_id++;
    }

    myProject->time = 0;
    myProject->duration = 0;

    //setting everything from -1
    {
        Layer* layer = project->layers;
        MyLayer* myLayer = myProject->myLayers;
        for(; myLayer != NULL; myLayer = myLayer->next, layer = layer->next){
            size_t myMedias_count = 0;
            for(MyMedia* myMedia = myLayer->myMedias; myMedia != NULL; myMedia = myMedia->next) myMedias_count++;

            double layerDuration = 0;
            for(Slice* slice = layer->slices; slice != NULL; slice = slice->next){
                if(slice->duration == -1){
                    if(slice->media_index == EMPTY_MEDIA){
                        fprintf(stderr, "You cannot have duration of -1 in Empty media\n");
                        return false;
                    }
                    if(slice->media_index >= myMedias_count){
                        fprintf(stderr, "Media %zu doesnt exist\n", slice->media_index);
                        return false;
                    }
                    MyMedia* media = ll_at(myLayer->myMedias, slice->media_index);
                    if(media->media.isImage){
                        fprintf(stderr, "You cannot have duration of -1 in Image media\n");
                        return false;
                    }

                    slice->duration = media->duration - slice->offset;
                }
                layerDuration += slice->duration;
            }
            if(layerDuration > myProject->duration) myProject->duration = layerDuration;
        }
    }

    {
        Layer* layer = project->layers;
        MyLayer* myLayer = myProject->myLayers;
        for(; myLayer != NULL; myLayer = myLayer->next, layer = layer->next){
            if(!updateSlice(myLayer->myMedias,layer->slices, myLayer->args.currentSlice, &myLayer->args.currentMediaIndex, &myLayer->args.checkDuration)) return false;
            if(myLayer->args.currentMediaIndex == EMPTY_MEDIA) continue;
            MyMedia* myMedia = ll_at(myLayer->myMedias, myLayer->args.currentMediaIndex);
            Slice* slice = ll_at(layer->slices, myLayer->args.currentSlice);
            if(myMedia->hasVideo) myLayer->args.lastVideoPts = slice->offset / av_q2d(myMedia->media.videoStream->time_base);
            printf("[FVFX] Processing Layer %s Slice 1!\n", hrp_name(&myLayer->args));
        }
    }
    return true;
}

int process_project(VkCommandBuffer cmd, Project* project, MyProject* myProject, Vulkanizer* vulkanizer, void* push_constants_buf, VkImageView outComposedImageView, bool* enoughSamplesOUT){
    *enoughSamplesOUT = true;
    size_t finishedCount = 0;
    size_t i = 0;
    Layer* layer = project->layers;
    MyLayer* myLayer = myProject->myLayers;
    size_t layers_count = 0;
    for(; myLayer != NULL; myLayer = myLayer->next, layer = layer->next){
        layers_count++;
        if(myLayer->finished) {
            finishedCount++;
            continue;
        }
        myLayer->volume = VfxLayerSoundParameter_Evaluate(&layer->volume, myProject->time);
        myLayer->pan = VfxLayerSoundParameter_Evaluate(&layer->pan, myProject->time);
        myProject->vulkanizerVfxInstances.count = 0;
        for(VfxInstance* vfx = layer->vfxInstances; vfx != NULL; vfx = vfx->next){
            if((vfx->duration != -1) && !(myProject->time > vfx->offset && myProject->time < vfx->offset + vfx->duration)) continue;

            if(vfx->inputs != NULL) VfxInstance_Update(myProject->myVfxs, vfx, myProject->time, push_constants_buf);
            MyVfx* myVfx = ll_at(myProject->myVfxs, vfx->vfx_index);
            da_append(&myProject->vulkanizerVfxInstances, ((VulkanizerVfxInstance){.vfx = &myVfx->vfx, .push_constants_data = push_constants_buf, .push_constants_size = myVfx->vfx.module->pushContantsSize}));
        }

        int e = getFrame(cmd, vulkanizer, project, layer->slices, myLayer->myMedias, &myProject->vulkanizerVfxInstances, &myLayer->frame, myLayer->audioFifo, &myLayer->args, outComposedImageView);
        
        MyMedia* myMedia = ll_at(myLayer->myMedias, myLayer->args.currentMediaIndex);
        if(myLayer->audioFifo && (myLayer->args.currentMediaIndex == EMPTY_MEDIA || (myLayer->args.currentMediaIndex != EMPTY_MEDIA && !myMedia->hasAudio))){
            av_audio_fifo_add_silence(myLayer->audioFifo, myProject->myLayers_fifo_fmt, &myProject->myLayers_fifo_ch_layout, project->settings.sampleRate / project->settings.fps);
        }

        if(myLayer->audioFifo && myLayer->args.currentMediaIndex != EMPTY_MEDIA && av_audio_fifo_size(myLayer->audioFifo) < myProject->myLayers_fifo_frame_size) *enoughSamplesOUT = false;
        if(e == -GET_FRAME_ERR) return 1;
        if(e == -GET_FRAME_FINISHED) {printf("[FVFX] Layer %s finished\n", hrp_name(&myLayer->args));myLayer->finished = true; finishedCount++; continue;}
        if(e == -GET_FRAME_SKIP) continue;
    }
    myProject->time += 1.0 / project->settings.fps;
    return finishedCount == layers_count ? PROCESS_PROJECT_FINISHED : PROCESS_PROJECT_CONTINUE;
}

bool project_seek(Project* project, MyProject* myProject, double time_seconds) {
    myProject->time = time_seconds;

    size_t i = 0;
    Layer* layer = project->layers;
    MyLayer* myLayer = myProject->myLayers;
    for(; myLayer != NULL; myLayer = myLayer->next, layer = layer->next){
        myLayer->args.currentSlice = 0;
        myLayer->args.currentMediaIndex = EMPTY_MEDIA;
        myLayer->args.checkDuration = 0;
        myLayer->args.localTime = 0;
        myLayer->args.video_skip_count = 0;
        myLayer->args.times_to_catch_up_target_framerate = 0;
        myLayer->finished = false;

        if(myLayer->audioFifo) av_audio_fifo_reset(myLayer->audioFifo);

        double accumulatedTime = 0.0;
        bool sliceFound = false;
        size_t slice_index = 0;
        for(Slice* slice = layer->slices; slice != NULL; slice = slice->next, slice_index++){
            double sliceStart = accumulatedTime;
            double sliceEnd = accumulatedTime + slice->duration;

            if (time_seconds >= sliceStart && time_seconds < sliceEnd) {
                myLayer->args.currentSlice = slice_index;
                myLayer->args.currentMediaIndex = slice->media_index;
                myLayer->args.checkDuration = slice->duration;
                myLayer->args.localTime = time_seconds - sliceStart;

                if (myLayer->args.currentMediaIndex != EMPTY_MEDIA) {
                    MyMedia* media = ll_at(myLayer->myMedias,myLayer->args.currentMediaIndex);

                    if (!media->media.isImage) {
                        if(!ffmpegMediaSeek(&media->media, slice->offset + myLayer->args.localTime)) {
                            fprintf(stderr, "ffmpegMediaSeek failed while seeking layer %zu media %zu\n", i, myLayer->args.currentMediaIndex);
                            return false;
                        }

                        if (media->hasVideo) {
                            myLayer->args.lastVideoPts = (slice->offset + myLayer->args.localTime) / av_q2d(media->media.videoStream->time_base);
                        }
                    }
                }

                sliceFound = true;
                break;
            }

            accumulatedTime = sliceEnd;
        }

        if (!sliceFound) {
            myLayer->finished = true;
            myLayer->args.currentSlice = slice_index;
            myLayer->args.currentMediaIndex = EMPTY_MEDIA;
            myLayer->args.localTime = 0;
        }
    }

    return true;
}

static void freeMyMedia(VkDevice device, VkDescriptorPool descriptorPool, MyMedia* media) {
    if (!media) return;

    ffmpegMediaUninit(&media->media);

    // Free Vulkan image resources
    if (media->mediaImageView)
        vkDestroyImageView(device, media->mediaImageView, NULL);
    if (media->mediaImage)
        vkDestroyImage(device, media->mediaImage, NULL);
    if (media->mediaImageMemory)
        vkFreeMemory(device, media->mediaImageMemory, NULL);
    if (media->mediaDescriptorSet)
        vkFreeDescriptorSets(device, descriptorPool, 1, &media->mediaDescriptorSet);
}

static void freeMyMedias(VkDevice device, VkDescriptorPool descriptorPool, MyMedia* medias) {
    if (!medias) return;

    for(MyMedia* myMedia = medias; myMedia != NULL; myMedia = myMedia->next)
        freeMyMedia(device, descriptorPool, myMedia);
}

static void freeMyLayer(VkDevice device, VkDescriptorPool descriptorPool, MyLayer* layer) {
    if (!layer) return;

    // Free media collection
    freeMyMedias(device, descriptorPool, layer->myMedias);

    // Free audio FIFO
    if (layer->audioFifo)
        av_audio_fifo_free(layer->audioFifo);
}

static void freeMyLayers(VkDevice device, VkDescriptorPool descriptorPool, MyLayer* layers) {
    if (!layers) return;

    for(MyLayer* myLayer = layers; myLayer != NULL; myLayer = myLayer->next)
        freeMyLayer(device, descriptorPool, myLayer);
}

static void freeVulkanizerVfx(VkDevice device, VulkanizerVfx* vfx){
    vkDestroyPipelineLayout(device, vfx->pipelineLayout, NULL);
    vkDestroyPipeline(device, vfx->pipeline, NULL);
}

static void freeMyVfxs(VkDevice device, MyVfx* vfxs) {
    if (!vfxs) return;

    for(MyVfx* myVfx = vfxs; myVfx != NULL; myVfx = myVfx->next)
        freeVulkanizerVfx(device, &myVfx->vfx);
}

void project_uninit(Vulkanizer* vulkanizer, MyProject* myProject, ArenaAllocator* aa){
    if (!myProject) return;

    freeMyLayers(vulkanizer->device, vulkanizer->descriptorPool, myProject->myLayers);
    freeMyVfxs(vulkanizer->device, myProject->myVfxs);
    aa_reset(aa);

    *myProject = (MyProject){0};
}