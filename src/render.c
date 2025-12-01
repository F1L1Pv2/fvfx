#include "render.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "engine/vulkan_simple.h"
#include "vulkanizer.h"
#include "ffmpeg_media.h"
#include "ffmpeg_media_render.h"
#include "ffmpeg_helper.h"
#include "myProject.h"
#include <math.h>
#include "ll.h"
#include "fvfx_helper.h"

int render(Project* project, ArenaAllocator* aa){
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
    if(!Vulkanizer_init(device, descriptorPool, project->settings.width, project->settings.height, &vulkanizer, aa)) return 1;

    //init renderer
    MediaRenderContext renderContext = {0};
    if(!ffmpegMediaRenderInit(project->settings.outputFilename, project->settings.width, project->settings.height, project->settings.fps, project->settings.sampleRate, project->settings.stereo, project->settings.hasAudio, &renderContext)){
        fprintf(stderr, "Couldn't initialize ffmpeg media renderer!\n");
        return 1;
    }

    enum AVSampleFormat out_audio_format = renderContext.audioCodecContext->sample_fmt;
    size_t out_audio_frame_size = renderContext.audioCodecContext->frame_size;

    MyProject myProject = {0};
    if(!prepare_project(project, &myProject, &vulkanizer, out_audio_format, out_audio_frame_size, aa)) return 1;

    uint8_t** tempAudioBuf;
    int tempAudioBufLineSize;
    av_samples_alloc_array_and_samples(&tempAudioBuf,&tempAudioBufLineSize, project->settings.stereo ? 2 : 1, out_audio_frame_size, out_audio_format, 0);

    uint8_t** composedAudioBuf;
    int composedAudioBufLineSize;
    av_samples_alloc_array_and_samples(&composedAudioBuf,&composedAudioBufLineSize, project->settings.stereo ? 2 : 1, out_audio_frame_size, out_audio_format, 0);

    void* push_constants_buf = calloc(256, sizeof(uint8_t));

    VkImage outComposedImage;
    VkDeviceMemory outComposedImageMemory;
    VkImageView outComposedImageView;
    size_t outComposedImage_stride;
    void* outComposedImage_mapped;
    uint32_t* outComposedVideoFrame = malloc(project->settings.width*project->settings.height*sizeof(uint32_t));

    if(!createMyImage(device, &outComposedImage, 
        project->settings.width, project->settings.height, 
        &outComposedImageMemory, 
        &outComposedImageView, 
        &outComposedImage_stride, 
        &outComposedImage_mapped,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    )) return 1;

    VkCommandBuffer tempCmd = vkCmdBeginSingleTime();
    vkCmdTransitionImage(tempCmd, outComposedImage, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
    vkCmdEndSingleTime(tempCmd);

    MyLayer* myLayers = myProject.myLayers;
    while(true){
        vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &inFlightFence);
        
        vkResetCommandBuffer(cmd, 0);
        vkBeginCommandBuffer(cmd,&(VkCommandBufferBeginInfo){
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = 0,
            .pInheritanceInfo = NULL,
        });

        Vulkanizer_reset_pool();

        vkCmdTransitionImage(
            cmd,
            outComposedImage,
            VK_IMAGE_LAYOUT_GENERAL, 
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        vkCmdBeginRenderingEX(cmd,
            .colorAttachment = outComposedImageView,
            .clearColor = COL_EMPTY,
            .renderArea = (
                (VkExtent2D){.width = project->settings.width, .height= project->settings.height}
            )
        );

        vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
            .width = project->settings.width,
            .height = project->settings.height
        });
            
        vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
            .extent.width = project->settings.width,
            .extent.height = project->settings.height,
        });

        vkCmdEndRendering(cmd);

        bool enoughSamples;
        int result = process_project(cmd, project, &myProject, &vulkanizer, push_constants_buf, outComposedImageView, &enoughSamples);
        if(result == PROCESS_PROJECT_FINISHED) break;

        vkCmdTransitionImage(
            cmd,
            outComposedImage,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
            VK_IMAGE_LAYOUT_GENERAL, 
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        vkEndCommandBuffer(cmd);

        vkQueueSubmit(graphicsQueue, 1, &(VkSubmitInfo){
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
        }, inFlightFence);
        vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);

        for(size_t y = 0; y < project->settings.height; y++){
            memcpy(
                ((uint8_t*)outComposedVideoFrame) + y*project->settings.width*sizeof(uint32_t),
                ((uint8_t*)outComposedImage_mapped) + y*outComposedImage_stride,
                project->settings.width*sizeof(uint32_t)
            );
        }

        ffmpegMediaRenderPassFrame(&renderContext, &(RenderFrame){
            .type = RENDER_FRAME_TYPE_VIDEO,
            .data = outComposedVideoFrame,
            .size = project->settings.width * project->settings.height * sizeof(outComposedVideoFrame[0]),
        });

        if(enoughSamples){
            av_samples_set_silence(composedAudioBuf, 0, out_audio_frame_size, project->settings.stereo ? 2 : 1, out_audio_format);
            mix_all_layers(
                composedAudioBuf,
                tempAudioBuf,
                myLayers,
                out_audio_frame_size,
                out_audio_format,
                project
            );
            ffmpegMediaRenderPassFrame(&renderContext, &(RenderFrame){
                .type = RENDER_FRAME_TYPE_AUDIO,
                .data = composedAudioBuf,
                .size = out_audio_frame_size,
            });
        }
    }

    printf("[FVFX] Draining leftover audio\n");
    bool audioLeft = true;
    while (audioLeft) {
        audioLeft = false;
        for(MyLayer* myLayer = myLayers; myLayer != NULL; myLayer = myLayer->next){
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
            out_audio_frame_size,
            project->settings.stereo ? 2 : 1,
            out_audio_format
        );
        mix_all_layers(
            composedAudioBuf,
            tempAudioBuf,
            myLayers,
            out_audio_frame_size,
            out_audio_format,
            project
        );
        ffmpegMediaRenderPassFrame(&renderContext, &(RenderFrame){
            .type = RENDER_FRAME_TYPE_AUDIO,
            .data = composedAudioBuf,
            .size = out_audio_frame_size,
        });
    }

    ffmpegMediaRenderFinish(&renderContext);
    printf("[FVFX] Finished rendering!\n");

    return 0;
}