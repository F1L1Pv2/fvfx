#include "render.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "engine/vulkan_simple.h"
#include "vulkanizer.h"
#include "ffmpeg_media.h"
#include "ffmpeg_helper.h"
#include "myProject.h"
#include <math.h>
#include "thirdparty/miniaudio.h"
#include "dd.h"
#include "loader.h"

typedef struct{
    Project* project;
    MyProject* myProject;
    uint8_t** tempAudioBuf;
    enum AVSampleFormat out_audio_format;
    bool* paused;
    float* global_volume;
} MiniaudioUserData;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pInput;
    memset(pOutput, 0, frameCount*sizeof(float)*pDevice->playback.channels);

    MiniaudioUserData* data = (MiniaudioUserData*)pDevice->pUserData;
    if(*data->paused) return;
    Project* project = data->project;
    MyLayers* myLayers = &data->myProject->myLayers;
    uint8_t** tempAudioBuf = data->tempAudioBuf;

    for(size_t i = 0; i < myLayers->count; i++){
        MyLayer* myLayer = &myLayers->items[i];
        if(!myLayer->audioFifo) continue;
        int read = av_audio_fifo_read(myLayer->audioFifo, (void**)tempAudioBuf, frameCount);
        bool conditionalMix = (myLayer->finished) || (myLayer->args.currentMediaIndex == EMPTY_MEDIA) || (myLayer->args.currentMediaIndex != EMPTY_MEDIA && !myLayer->myMedias.items[myLayer->args.currentMediaIndex].hasAudio);
        if(conditionalMix && read > 0){
            mix_audio((uint8_t **)&pOutput, tempAudioBuf, read, project->stereo ? 2 : 1, data->out_audio_format);
            continue;
        }else if(conditionalMix && read == 0) continue;
        
        mix_audio((uint8_t **)&pOutput, tempAudioBuf, read, project->stereo ? 2 : 1, data->out_audio_format);
    }
    for(size_t i = 0; i < frameCount;i++){
        ((float*)pOutput)[i*2 + 0] *= *data->global_volume;
        ((float*)pOutput)[i*2 + 1] *= *data->global_volume;
    }
}

typedef struct {
    float v[16];
} mat4;

static mat4 ortho2D(float width, float height){
    float left = -width/2;
    float right = width/2;
    float top = height/2;
    float bottom = -height/2;

    return (mat4){
    2 / (right - left),0                 , 0, -(right + left) / (right - left),
          0           ,2 / (top - bottom), 0, -(top + bottom) / (top - bottom),
          0           ,     0            ,-1,                 0,
          0           ,     0            , 0,                 1,
    };
}

static mat4 mat4mul(mat4 *a, mat4 *b) {
    mat4 result;

    // Column-major multiplication: result[i][j] = sum_k a[k][j] * b[i][k]
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                // a is row × k, b is k × col
                float a_elem = a->v[k * 4 + row]; // a[row][k]
                float b_elem = b->v[col * 4 + k]; // b[k][col]
                sum += a_elem * b_elem;
            }
            result.v[col * 4 + row] = sum; // result[row][col]
        }
    }

    return result;
}

typedef struct{
    float pos_x;
    float pos_y;
    float scale_x;
    float scale_y;
    mat4 projView;
} PreviewPushConstants;

typedef struct{
    float x;
    float y;
    float width;
    float height;
} Rect;

static Rect fitRectangle(Rect outer, float innerWidth, float innerHeight){
    Rect out = {0};
    out.x = outer.x;
    out.y = outer.y;

    float innerAspect = (float)innerWidth / (float)innerHeight;
    float outerAspect = outer.width / outer.height;

    if (outerAspect < innerAspect) {
        float scaledH = outer.width / innerAspect;
        float yOffset = (outer.height - scaledH) * 0.5f;

        out.width = outer.width;
        out.height = scaledH;
        out.y += yOffset;
    } else {
        float scaledW = outer.height * innerAspect;
        float xOffset = (outer.width - scaledW) * 0.5f;
        
        out.width = scaledW;
        out.height = outer.height;
        out.x += xOffset;
    }

    return out;
}

static bool pointInsideRect(float x, float y, Rect rect){
    return !(
        (x < rect.x) ||
        (x > rect.x + rect.width) ||
        (y < rect.y) ||
        (y > rect.y + rect.height)
    );
}

#define PREVIEW_WIDTH_SCALER (0.75)
#define PREVIEW_HEIGHT_SCALER (0.75)

int preview(Project* project, const char* project_filename, int argc, const char** argv, StringAllocator* sa){
    if(!vulkan_init_with_window("FVFX", 640, 480)) return 1;

    // TODO: optimize this byh even more
    project->width *= PREVIEW_WIDTH_SCALER;
    project->height *= PREVIEW_HEIGHT_SCALER;

    VkCommandBuffer cmd;
    if(vkAllocateCommandBuffers(device,&(VkCommandBufferAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    },&cmd) != VK_SUCCESS) return 1;
        
    VkFence renderingFence;
    if(vkCreateFence(device, &(VkFenceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    }, NULL, &renderingFence) != VK_SUCCESS) return 1;

    VkSemaphore swapchainHasImageSemaphore;
    if(vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}, NULL, &swapchainHasImageSemaphore) != VK_SUCCESS) return 1;
    VkSemaphore readyToSwapYourChainSemaphore;
    if(vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}, NULL, &readyToSwapYourChainSemaphore) != VK_SUCCESS) return 1;

    Vulkanizer vulkanizer = {0};
    if(!Vulkanizer_init(device, descriptorPool, project->width, project->height, &vulkanizer, sa)) return 1;

    if(!dd_init(device, swapchainImageFormat, descriptorPool)) return 1;

    enum AVSampleFormat out_audio_format = AV_SAMPLE_FMT_FLT;
    size_t out_audio_frame_size = project->sampleRate/100;

    MyProject myProject = {0};
    if(!prepare_project(project, &myProject, &vulkanizer, out_audio_format, out_audio_frame_size)) return 1;

    VulkanizerVfxInstances vulkanizerVfxInstances = {0};
    void* push_constants_buf = calloc(256, sizeof(uint8_t));

    VkImage          outComposedImage;
    VkDeviceMemory   outComposedImageMemory;
    VkImageView      outComposedImageView;
    VkDescriptorSet  outComposedImage_set;
    uint32_t*        outComposedVideoFrame = malloc(project->width*project->height*sizeof(uint32_t));

    if(!createMyImage(device, &outComposedImage, 
        project->width, project->height, 
        &outComposedImageMemory, 
        &outComposedImageView, 
        NULL, 
        NULL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        0
    )) return 1;

    VkPipeline previewPipeline;
    VkPipelineLayout previewPipelineLayout;

    {
        VkShaderModule vertexShader;
        const char* vertexShaderSrc = 
            "#version 450\n"
            "layout(location = 0) out vec2 uv;\n"
            "layout(push_constant) uniform Constants {\n"
            "    vec2 position;\n"
            "    vec2 scale;\n"
            "    mat4 projView;\n"
            "} pcs;\n"
            "void main() {"
                "uint b = 1 << (gl_VertexIndex % 6);"
                "vec2 baseCoord = vec2((0x1C & b) != 0, (0xE & b) != 0);"
                "uv = baseCoord;"
                "mat4 model = mat4(vec4(pcs.scale.x, 0,0,0),vec4(0,pcs.scale.y,0,0),vec4(0,0,1,0),vec4(pcs.position.x,pcs.position.y,0,1));\n"
                "gl_Position = pcs.projView * model * vec4(baseCoord, 0.0, 1.0);"
            "}";


        if(!vkCompileShader(device,vertexShaderSrc, shaderc_vertex_shader, &vertexShader)) return 1;

        const char* fragmentShaderSrc =
            "#version 450\n"
            "layout(location = 0) out vec4 outColor;\n"
            "layout(location = 0) in vec2 uv;\n"
            "layout(set = 0, binding = 0) uniform sampler2D imageIN;\n"
            "void main() {\n"
                "outColor = texture(imageIN, uv);\n"
            "}\n";

        VkShaderModule fragmentShader;
        if(!vkCompileShader(device,fragmentShaderSrc, shaderc_fragment_shader, &fragmentShader)) return 1;

        if(!vkCreateGraphicPipeline(
            vertexShader,fragmentShader, 
            &previewPipeline, 
            &previewPipelineLayout,
            swapchainImageFormat,
            .descriptorSetLayoutCount = 1,
            .descriptorSetLayouts = &vulkanizer.vfxDescriptorSetLayout,
            .pushConstantsSize = sizeof(PreviewPushConstants),
        )) return 1;

        if(vkAllocateDescriptorSets(device, &(VkDescriptorSetAllocateInfo){
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &vulkanizer.vfxDescriptorSetLayout,
        }, &outComposedImage_set) != VK_SUCCESS) return 1;

        vkUpdateDescriptorSets(device, 1, &(VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dstSet = outComposedImage_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .pImageInfo = &(VkDescriptorImageInfo){
                .sampler = vulkanizer.samplerLinear,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .imageView = outComposedImageView,
            },
        }, 0, NULL);
    }

    {
        VkCommandBuffer tempCmd = vkCmdBeginSingleTime();
        vkCmdTransitionImage(tempCmd, outComposedImage, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        vkCmdEndSingleTime(tempCmd);
    }

    bool paused = false;
    float global_volume = 1.0;

    uint8_t** tempAudioBuf;
    int tempAudioBufLineSize;
    av_samples_alloc_array_and_samples(&tempAudioBuf,&tempAudioBufLineSize, project->stereo ? 2 : 1, out_audio_frame_size, out_audio_format, 0);

    //miniaudio init
    ma_device audio_device;
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_f32;
    deviceConfig.playback.channels = project->stereo ? 2 : 1;
    deviceConfig.sampleRate        = project->sampleRate;
    deviceConfig.dataCallback      = data_callback;
    deviceConfig.pUserData         = &(MiniaudioUserData){
        .project = project,
        .myProject = &myProject,
        .tempAudioBuf = tempAudioBuf,
        .out_audio_format = out_audio_format,
        .paused = &paused,
        .global_volume = &global_volume,
    };

    if (ma_device_init(NULL, &deviceConfig, &audio_device) != MA_SUCCESS) {
        printf("Failed to open playback device.\n");
        return -3;
    }

    if (ma_device_start(&audio_device) != MA_SUCCESS) {
        printf("Failed to start playback device.\n");
        ma_device_uninit(&audio_device);
        return -4;
    }

    VkExtent2D oldSwapchainExtent = {0};
    mat4 projView = {0};
    Rect previewRect;
    PreviewPushConstants pcs;

    uint32_t imageIndex;
    uint64_t oldTime = platform_get_time_nanos();

    double timeline_height = 0;
    double timeline_y = 0;
    Rect timelineRect = {0};

    bool scrubbed = false;
    bool hotReloaded = false;

    while(platform_still_running()){
        platform_window_handle_events();
        if(platform_window_minimized){
            platform_sleep(1);
            continue;
        }

        hotReloaded = false;
        if(input.keys[KEY_R].justPressed) hotReloaded = true;

        scrubbed = false;

        if(input.scroll != 0){
            global_volume += (double)input.scroll/3000.0;
            if(global_volume < 0) global_volume = 0;
            if(global_volume > 3) global_volume = 3;
        }

        if(oldSwapchainExtent.width != swapchainExtent.width || oldSwapchainExtent.height != swapchainExtent.height){
            oldSwapchainExtent = swapchainExtent;
            mat4 proj = ortho2D(swapchainExtent.width,swapchainExtent.height); 
            projView = mat4mul(&proj, &(mat4){
                1,0,0,0,
                0,1,0,0,
                0,0,1,0,
                -((float)swapchainExtent.width)/2, -((float)swapchainExtent.height)/2, 0, 1,
            });

            timeline_height = swapchainExtent.height/8;
            timeline_y = swapchainExtent.height - timeline_height;

            timelineRect = (Rect){
                .x = 0,
                .y = timeline_y,
                .width = swapchainExtent.width,
                .height = timeline_height,
            };

            previewRect = fitRectangle((Rect){
                .x = 0,
                .y = 0,
                .width = swapchainExtent.width,
                .height = timeline_y,
            }, project->width, project->height);

            pcs = (PreviewPushConstants){
                .pos_x = previewRect.x,
                .pos_y = previewRect.y,
                .scale_x = previewRect.width,
                .scale_y = previewRect.height,
                .projView = projView,
            };
        }

        uint64_t now = platform_get_time_nanos();
        oldTime = now;

        if(input.keys[KEY_MOUSE_LEFT].isDown && pointInsideRect(input.mouse_x, input.mouse_y, timelineRect)){
            project_seek(project, &myProject,((double)input.mouse_x - timelineRect.x)/timelineRect.width*myProject.duration);
            scrubbed = true;
        }

        if(input.keys[KEY_SPACE].justPressed) paused = !paused;
        if(scrubbed == false && hotReloaded == false && paused) continue;

        dd_begin();

        dd_rect(timelineRect.x, timelineRect.y, timelineRect.width, timelineRect.height, 0xFF101010);

        dd_rect((myProject.time / myProject.duration)*swapchainExtent.width,timeline_y,5,timeline_height, 0xFFFF0000);

        {
            char buf[128];
            snprintf(buf,sizeof(buf),"%.02f/%.02f volume: %zu%%", myProject.time, myProject.duration, (size_t)(global_volume*100.0));
            double textSize = 20;
            dd_text(buf, swapchainExtent.width/2 - dd_text_measure(buf,textSize)/2, timeline_y, textSize, 0xFFFFFFFF);
        }

        dd_end();

        vkWaitForFences(device, 1, &renderingFence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &renderingFence);

        if(hotReloaded){
            ma_device_stop(&audio_device);
            ma_device_uninit(&audio_device);
            double time = myProject.time;
            project_uninit(&vulkanizer, &myProject, sa);
            project_loader_clean(project, sa);
            if (tempAudioBuf) {
                av_freep(&tempAudioBuf[0]); // Frees the actual audio buffer(s)
                av_freep(&tempAudioBuf);    // Frees the array of pointers
            }
            
            /*
            outComposedImage
            outComposedImageMemory
            outComposedImageView
            outComposedImage_set
            outComposedVideoFrame
            */

            vkDestroyImageView(device, outComposedImageView, NULL);
            vkDestroyImage(device, outComposedImage, NULL);
            vkFreeMemory(device, outComposedImageMemory, NULL);
            free(outComposedVideoFrame);

            if(!project_loader_load(project, project_filename, argc, argv, sa)) return 1;
            project->width *= PREVIEW_WIDTH_SCALER;
            project->height *= PREVIEW_HEIGHT_SCALER;

            vulkanizer.videoOutWidth = project->width;
            vulkanizer.videoOutHeight = project->height;

            out_audio_frame_size = project->sampleRate/100;
            if(!prepare_project(project, &myProject, &vulkanizer, out_audio_format, out_audio_frame_size)) return 1;

            if(!createMyImage(device, &outComposedImage, 
                project->width, project->height, 
                &outComposedImageMemory, 
                &outComposedImageView, 
                NULL, 
                NULL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                0
            )) return 1;
            outComposedVideoFrame = malloc(project->width*project->height*sizeof(uint32_t));

            vkUpdateDescriptorSets(device, 1, &(VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dstSet = outComposedImage_set,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .pImageInfo = &(VkDescriptorImageInfo){
                    .sampler = vulkanizer.samplerLinear,
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .imageView = outComposedImageView,
                },
            }, 0, NULL);

            VkCommandBuffer tempCmd = vkCmdBeginSingleTime();
            vkCmdTransitionImage(tempCmd, outComposedImage, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            vkCmdEndSingleTime(tempCmd);

            av_samples_alloc_array_and_samples(&tempAudioBuf,&tempAudioBufLineSize, project->stereo ? 2 : 1, out_audio_frame_size, out_audio_format, 0);
            
            deviceConfig = ma_device_config_init(ma_device_type_playback);
            deviceConfig.playback.format   = ma_format_f32;
            deviceConfig.playback.channels = project->stereo ? 2 : 1;
            deviceConfig.sampleRate        = project->sampleRate;
            deviceConfig.dataCallback      = data_callback;
            deviceConfig.pUserData         = &(MiniaudioUserData){
                .project = project,
                .myProject = &myProject,
                .tempAudioBuf = tempAudioBuf,
                .out_audio_format = out_audio_format,
                .paused = &paused,
                .global_volume = &global_volume,
            };

            if (ma_device_init(NULL, &deviceConfig, &audio_device) != MA_SUCCESS) {
                printf("Failed to open playback device.\n");
                return -3;
            }

            if (ma_device_start(&audio_device) != MA_SUCCESS) {
                printf("Failed to start playback device.\n");
                ma_device_uninit(&audio_device);
                return -4;
            }

            if(time < myProject.duration) {
                project_seek(project, &myProject, time);
            }
        }

        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, swapchainHasImageSemaphore, NULL, &imageIndex);
        
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
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        );

        vkCmdBeginRenderingEX(cmd,
            .colorAttachment = outComposedImageView,
            .clearColor = COL_BLACK,
            .renderArea = (
                (VkExtent2D){.width = project->width, .height= project->height}
            )
        );

        vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
            .width = project->width,
            .height = project->height
        });
            
        vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
            .extent.width = project->width,
            .extent.height = project->height,
        });

        vkCmdEndRendering(cmd);

        bool enoughSamples;
        int result = process_project(cmd, project, &myProject, &vulkanizer, &vulkanizerVfxInstances, push_constants_buf, outComposedImageView, &enoughSamples);
        if(result == PROCESS_PROJECT_FINISHED) {
            if(!project_seek(project, &myProject,0)) break;
        }

        vkCmdTransitionImage(
            cmd,
            outComposedImage,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        );

        vkCmdTransitionImage(cmd, swapchainImages.items[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        vkCmdBeginRenderingEX(cmd,
            .colorAttachment = swapchainImageViews.items[imageIndex],
            .clearColor = COL_HEX(0xFF181818),
            .renderArea = (
                (VkExtent2D){.width = swapchainExtent.width, .height= swapchainExtent.height}
            )
        );

        vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
            .width = swapchainExtent.width,
            .height = swapchainExtent.height
        });
            
        vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
            .extent.width = swapchainExtent.width,
            .extent.height = swapchainExtent.height,
        });

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, previewPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, previewPipelineLayout, 0, 1, &outComposedImage_set,0,NULL);
        vkCmdPushConstants(cmd, previewPipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(PreviewPushConstants),&pcs);
        vkCmdDraw(cmd,6,1,0,0);

        vkCmdEndRendering(cmd);

        dd_draw(cmd, swapchainExtent.width, swapchainExtent.height, swapchainImageViews.items[imageIndex]);

        vkCmdTransitionImage(cmd, swapchainImages.items[imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        vkEndCommandBuffer(cmd);

        vkQueueSubmit(graphicsQueue, 1, &(VkSubmitInfo){
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            
            .waitSemaphoreCount = 1,
            .pWaitDstStageMask = &(VkPipelineStageFlags){VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
            .pWaitSemaphores = &swapchainHasImageSemaphore,

            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &readyToSwapYourChainSemaphore,
        }, renderingFence);

        vkQueuePresentKHR(presentQueue, &(VkPresentInfoKHR){
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &readyToSwapYourChainSemaphore,

            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIndex
        });

        uint64_t frameEnd = platform_get_time_nanos();
        double frameTime = (double)(frameEnd - now) * 1e-9;
        if (frameTime < (1.0 / project->fps)) {
            platform_sleep(((1.0 / project->fps) - frameTime)*1000);
        }
    }

    ma_device_stop(&audio_device);
    ma_device_uninit(&audio_device);

    return 0;
}