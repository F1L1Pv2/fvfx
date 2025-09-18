#include "vulkan/vulkan.h"
#include "engine/vulkan_globals.h"
#include "engine/vulkan_initialize.h"
#include "engine/vulkan_getDevice.h"
#include "engine/vulkan_createSurface.h"
#include "engine/vulkan_initSwapchain.h"
#include "engine/vulkan_initCommandPool.h"
#include "engine/vulkan_initCommandBuffer.h"
#include "engine/vulkan_compileShader.h"
#include "engine/vulkan_synchro.h"
#include "engine/vulkan_buffer.h"
#include "engine/platform.h"
#include "engine/vulkan_createGraphicPipelines.h"
#include "engine/vulkan_initDescriptorPool.h"
#include "engine/vulkan_images.h"
#include "engine/vulkan_helpers.h"
#include "engine/vulkan_initSamplers.h"

#include "vulkanizer.h"
#include "shader_utils.h"

static VkImageView currentViewInDescriptor = NULL;
static void updateDescriptorIfNeeded(Vulkanizer* vulkanizer, VkImageView newView){
    if(newView == currentViewInDescriptor) return;

    // --------------------- updating descriptor set -------------------------
    VkDescriptorImageInfo descriptorImageInfo = {0};
    VkWriteDescriptorSet writeDescriptorSet = {0};

    descriptorImageInfo.sampler = samplerLinear;
    descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorImageInfo.imageView = newView;

    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.dstSet = vulkanizer->vfxDescriptorSet;
    writeDescriptorSet.dstBinding = 0;
    writeDescriptorSet.dstArrayElement = 0;
    writeDescriptorSet.pImageInfo = &descriptorImageInfo;

    vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);
    currentViewInDescriptor = newView;
}

void transitionMyImage_inner(VkCommandBuffer tempCmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlagBits oldStage, VkPipelineStageFlagBits newStage){
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        tempCmd,
        oldStage,
        newStage,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );
}

static bool applyShadersOnFrame(   Frame* frameIn,

                            void* outData,
                            size_t outWidth,
                            size_t outHeight,

                            void* frameInData,
                            size_t frameInStride,
                            
                            VkImage color, 
                            VkImageView colorAttachment, 
                            void* colorData, 
                            size_t colorStride,

                            VkPipeline pipeline, 
                            VkPipelineLayout pipelineLayout, 
                            VkDescriptorSet* descriptorSet
                        ){
    if(frameIn->type != FRAME_TYPE_VIDEO) return false;

    VideoFrame* frame = &frameIn->video;

    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFence);
    vkResetCommandBuffer(cmd, 0);

    for(int i = 0; i < frame->height; i++){
        memcpy(
            (uint8_t*)frameInData + frameInStride*i,
            (uint8_t*)frame->data + frame->width*sizeof(uint32_t)*i,
            frame->width *sizeof(uint32_t)
        );
    }

    VkCommandBufferBeginInfo commandBufferBeginInfo = {0};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBeginInfo.pNext = NULL;
    commandBufferBeginInfo.flags = 0;
    commandBufferBeginInfo.pInheritanceInfo = NULL;
    vkBeginCommandBuffer(cmd,&commandBufferBeginInfo);

    transitionMyImage_inner(cmd, color, 
        VK_IMAGE_LAYOUT_GENERAL, 
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_PIPELINE_STAGE_TRANSFER_BIT, 
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    vkCmdBeginRenderingEX(cmd,
        .colorAttachment = colorAttachment,
        .clearColor = COL_EMPTY,
        .renderArea = (
            (VkExtent2D){.width = outWidth, .height= outHeight}
        )
    );

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = outWidth,
        .height = outHeight
    });
        
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = (VkExtent2D){.width = outWidth, .height = outHeight},
    });

    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,descriptorSet,0,NULL);
    float constants[4] = {
        outWidth, outHeight,
        frame->width, frame->height
    };
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(constants), constants);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    vkCmdEndRendering(cmd);

    transitionMyImage_inner(cmd, color, 
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_IMAGE_LAYOUT_GENERAL, 
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence);
    vkQueueWaitIdle(graphicsQueue);

    for(size_t i = 0; i < outHeight; i++) {
        memcpy(
            (uint8_t*)outData + i * outWidth * sizeof(uint32_t),
            (uint8_t*)colorData + i * colorStride,
            outWidth * sizeof(uint32_t)
        );
    }

    return true;
}

void transitionMyImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlagBits oldStage, VkPipelineStageFlagBits newStage){
    VkCommandBuffer tempCmd = beginSingleTimeCommands();
    transitionMyImage_inner(tempCmd, image, oldLayout, newLayout, oldStage, newStage);
    endSingleTimeCommands(tempCmd);
}

bool createMyImage(VkImage* image, size_t width, size_t height, VkDeviceMemory* imageMemory, VkImageView* imageView, size_t* imageStride, void** imageMapped, VkImageUsageFlagBits imageUsage, VkMemoryPropertyFlagBits memoryProperty){
    if(!createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,
            imageUsage,
            memoryProperty, image,imageMemory)){
        printf("Couldn't create image\n");
        return false;
    }

    if(!createImageView(*image,VK_FORMAT_R8G8B8A8_UNORM, 
                VK_IMAGE_ASPECT_COLOR_BIT, imageView)){
        printf("Couldn't create image view\n");
        return false;
    }

    *imageStride = getImageStride(*image);
    vkMapMemory(device,*imageMemory, 0, (*imageStride)*height, 0, imageMapped);

    return true;
}

bool Vulkanizer_init(Vulkanizer* vulkanizer){
    if(!initialize_vulkan()) return false;
    if(!getDevice()) return false;
    if(!initCommandPool()) return false;
    if(!initCommandBuffer()) return false;
    if(!createAllNeededSyncrhonizationObjects()) return false;
    if(!initDescriptorPool()) return false;
    if(!initSamplers()) return false;

    const char* vertexShaderSrc = 
        "#version 450\n"
        "layout(location = 0) out vec2 uv;\n"
        "void main() {"
            "uint b = 1 << (gl_VertexIndex % 6);"
            "vec2 baseCoord = vec2((0x1C & b) != 0, (0xE & b) != 0);"
            "uv = baseCoord;"
            "gl_Position = vec4(baseCoord * 2 - 1, 0.0f, 1.0f);"
        "}";


    if(!compileShader(vertexShaderSrc, shaderc_vertex_shader, &vulkanizer->vertexShader)) return false;

    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {0};
    descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorSetLayoutBinding.descriptorCount = 1;
    descriptorSetLayoutBinding.binding = 0;
    descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {0};
    descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.bindingCount  = 1;
    descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;

    if(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &vulkanizer->vfxDescriptorSetLayout) != VK_SUCCESS){
        printf("ERROR\n");
        return false;
    }

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &vulkanizer->vfxDescriptorSetLayout;

    vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, &vulkanizer->vfxDescriptorSet);

    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    if(!Vulkanizer_init_vfx(vulkanizer, "./addons/fit.fvfx",&vulkanizer->vfx)) return false;
    return true;
}

bool Vulkanizer_init_image_for_media(size_t width, size_t height, VkImage* imageOut, VkDeviceMemory* imageMemoryOut, VkImageView* imageViewOut, size_t* imageStrideOut, void* imageDataOut){
    if(!createMyImage(imageOut,
        width, 
        height, 
        imageMemoryOut, imageViewOut, 
        imageStrideOut, 
        imageDataOut,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    )) return false;
    transitionMyImage(*imageOut, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    return true;
}

bool Vulkanizer_init_output_image(Vulkanizer* vulkanizer, size_t outWidth, size_t outHeight){
    if(!createMyImage(&vulkanizer->videoOutImage,
        outWidth, 
        outHeight, 
        &vulkanizer->videoOutImageMemory, &vulkanizer->videoOutImageView, 
        &vulkanizer->videoOutImageStride, 
        &vulkanizer->videoOutImageMapped,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    )) return false;
    transitionMyImage(vulkanizer->videoOutImage, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vulkanizer->videoOutWidth = outWidth;
    vulkanizer->videoOutHeight = outHeight;
    return true;
}

bool Vulkanizer_apply_vfx_on_frame(Vulkanizer* vulkanizer, VkImageView videoInView, void* videoInData, size_t videoInStride, Frame* frameIn, void* outData){
    updateDescriptorIfNeeded(vulkanizer, videoInView);

    return applyShadersOnFrame(
                frameIn,
                outData,
                vulkanizer->videoOutWidth,
                vulkanizer->videoOutHeight,

                videoInData,
                videoInStride,

                vulkanizer->videoOutImage, 
                vulkanizer->videoOutImageView, 
                vulkanizer->videoOutImageMapped, 
                vulkanizer->videoOutImageStride, 

                vulkanizer->vfx.pipeline, 
                vulkanizer->vfx.pipelineLayout, 
                &vulkanizer->vfxDescriptorSet);
}

static String_Builder sb = {0};
bool Vulkanizer_init_vfx(Vulkanizer* vulkanizer, const char* filename, VulkanizerVfx* outVfx){
    sb.count = 0;
    if(!read_entire_file(filename,&sb)) return false;
    if(!extractVFXModuleMetaData(nob_sb_to_sv(sb),&outVfx->module)) return false;
    if(!preprocessVFXModule(&sb, &outVfx->module)) return false;
    sb_append_null(&sb);

    VkShaderModule fragmentShader;
    if(!compileShader(sb.items,shaderc_fragment_shader,&fragmentShader)) return false;

    for(size_t i = 0; i < outVfx->module.inputs.count; i++){
        outVfx->module.pushContantsSize += get_vfxInputTypeSize(outVfx->module.inputs.items[i].type);
    }

    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    if(!createGraphicPipeline(
        vulkanizer->vertexShader,fragmentShader, 
        &outVfx->pipeline, 
        &outVfx->pipelineLayout,
        .pushConstantsSize = sizeof(float)*4 + outVfx->module.pushContantsSize,
        .descriptorSetLayoutCount = 1,
        .descriptorSetLayouts = &vulkanizer->vfxDescriptorSetLayout,
        .outColorFormat = &colorFormat
    )) return false;

    outVfx->module.filepath = filename;
    return true;
}