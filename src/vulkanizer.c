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

bool applyShadersOnFrame(   Frame* frameIn,
                            Frame* frameOut,

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
    VideoFrame* frameOutput = &frameOut->video;

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

    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = color;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );

    vkCmdBeginRenderingEX(cmd,
        .colorAttachment = colorAttachment,
        .clearColor = COL_EMPTY,
        .renderArea = (
            (VkExtent2D){.width = frame->width, .height= frame->height}
        )
    );

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = frame->width,
        .height = frame->height
    });
        
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = (VkExtent2D){.width = frame->width, .height = frame->height},
    });

    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,descriptorSet,0,NULL);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    vkCmdEndRendering(cmd);

    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
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

    for(size_t i = 0; i < frameOutput->height; i++) {
        memcpy(
            (uint8_t*)frameOutput->data + i * frameOutput->width * sizeof(uint32_t),
            (uint8_t*)colorData + i * colorStride,
            frameOutput->width * sizeof(uint32_t)
        );
    }

    return true;
}

void transitionMyImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlagBits oldStage, VkPipelineStageFlagBits newStage){
    VkCommandBuffer tempCmd = beginSingleTimeCommands();
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

    VkShaderModule vertexShader;

    if(!compileShader(vertexShaderSrc, shaderc_vertex_shader, &vertexShader)) return false;

    const char* fragmentShaderSrc = 
        "#version 450\n"
        "layout(location = 0) out vec4 outColor;\n"
        "layout(location = 0) in vec2 uv;\n"

        "layout (set = 0, binding = 0) uniform sampler2D imageIN;\n"

        "void main() {"
            "outColor = texture(imageIN, uv) * vec4(uv,1,1);"
        "}";

    VkShaderModule fragmentShader;
    if(!compileShader(fragmentShaderSrc, shaderc_fragment_shader, &fragmentShader)) return false;

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

    vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, &vulkanizer->outDescriptorSet);

    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    if(!createGraphicPipeline(
        vertexShader,fragmentShader, 
        &vulkanizer->pipeline, 
        &vulkanizer->pipelineLayout,
        .descriptorSetLayoutCount = 1,
        .descriptorSetLayouts = &vulkanizer->vfxDescriptorSetLayout,
        .outColorFormat = &colorFormat
    )) return false;

    return true;
}

bool Vulkanizer_init_images(Vulkanizer* vulkanizer, size_t width, size_t height){
    if(!createMyImage(&vulkanizer->videoInImage,
        width, 
        height, 
        &vulkanizer->videoInImageMemory, &vulkanizer->videoInImageView, 
        &vulkanizer->videoInImageStride, 
        &vulkanizer->videoInImageMapped,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    )) return false;
    transitionMyImage(vulkanizer->videoInImage, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    // --------------------- updating descriptor set -------------------------
    VkDescriptorImageInfo descriptorImageInfo = {0};
    VkWriteDescriptorSet writeDescriptorSet = {0};

    descriptorImageInfo.sampler = samplerLinear;
    descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorImageInfo.imageView = vulkanizer->videoInImageView;

    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.dstSet = vulkanizer->outDescriptorSet;
    writeDescriptorSet.dstBinding = 0;
    writeDescriptorSet.dstArrayElement = 0;
    writeDescriptorSet.pImageInfo = &descriptorImageInfo;

    vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);

    if(!createMyImage(&vulkanizer->videoOutImage,
        width, 
        height, 
        &vulkanizer->videoOutImageMemory, &vulkanizer->videoOutImageView, 
        &vulkanizer->videoOutImageStride, 
        &vulkanizer->videoOutImageMapped,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    )) return false;
    transitionMyImage(vulkanizer->videoOutImage, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    return true;
}

bool Vulkanizer_apply_vfx_on_frame(Vulkanizer* vulkanizer, Frame* frameIn, Frame* frameOut){
    return applyShadersOnFrame(
                frameIn,
                frameOut,
                vulkanizer->videoInImageMapped,
                vulkanizer->videoInImageStride,

                vulkanizer->videoOutImage, 
                vulkanizer->videoOutImageView, 
                vulkanizer->videoOutImageMapped, 
                vulkanizer->videoOutImageStride, 

                vulkanizer->pipeline, 
                vulkanizer->pipelineLayout, 
                &vulkanizer->outDescriptorSet);
}