#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "engine/vulkan_simple.h"
#include "vulkanizer.h"
#include "ffmpeg_media.h"
#include "ffmpeg_media_render.h"
#include "ffmpeg_helper.h"
#include "project.h"
#include "myProject.h"

#define NOB_STRIP_PREFIX
#include "nob.h"

#include <string.h>
#include <math.h>

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

    // ------------------------------------------- editor code -----------------------------------------------------
    if(!vulkan_init_headless()) return 1;

    VkCommandBuffer cmd;
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
    MyVfxs myVfxs = {0};
    if(!prepare_project(&project, &vulkanizer, &myLayers, &myVfxs, renderContext.audioCodecContext->sample_fmt, renderContext.audioCodecContext->frame_size)) return 1;
    
    RenderFrame renderFrame = {0};

    uint8_t** tempAudioBuf;
    int tempAudioBufLineSize;
    av_samples_alloc_array_and_samples(&tempAudioBuf,&tempAudioBufLineSize, project.stereo ? 2 : 1, renderContext.audioCodecContext->frame_size, renderContext.audioCodecContext->sample_fmt, 0);

    uint8_t** composedAudioBuf;
    int composedAudioBufLineSize;
    av_samples_alloc_array_and_samples(&composedAudioBuf,&composedAudioBufLineSize, project.stereo ? 2 : 1, renderContext.audioCodecContext->frame_size, renderContext.audioCodecContext->sample_fmt, 0);

    double projectTime = 0.0;
    VulkanizerVfxInstances vulkanizerVfxInstances = {0};
    void* push_constants_buf = calloc(256, sizeof(uint8_t));

    VkImage outComposedImage;
    VkDeviceMemory outComposedImageMemory;
    VkImageView outComposedImageView;
    size_t outComposedImage_stride;
    void* outComposedImage_mapped;
    uint32_t* outComposedVideoFrame = malloc(project.width*project.height*sizeof(uint32_t));

    if(!createMyImage(device, &outComposedImage, 
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

    if(!init_my_project(&project, &myLayers)) return false;

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

        bool enoughSamples;
        int result = process_project(cmd, &project, &vulkanizer, &myLayers, &myVfxs, &vulkanizerVfxInstances, projectTime, push_constants_buf, outComposedImageView, &enoughSamples);
        if(result == PROCESS_PROJECT_FINISHED) break;

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