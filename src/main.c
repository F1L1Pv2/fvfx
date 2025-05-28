#include <stdio.h>
#include <stdint.h>

#define NOB_STRIP_PREFIX
#include "nob.h"

#include "engine/vulkan_globals.h"
#include "engine/vulkan_initialize.h"
#include "engine/vulkan_getDevice.h"
#include "engine/vulkan_createSurface.h"
#include "engine/vulkan_initSwapchain.h"
#include "engine/vulkan_initCommandPool.h"
#include "engine/vulkan_initCommandBuffer.h"
#include "engine/vulkan_compileShader.h"
#include "engine/vulkan_initGraphicsPipeline.h"
#include "engine/vulkan_synchro.h"
#include "engine/vulkan_buffer.h"
#include "engine/platform.h"

typedef struct {
    float x;
    float y;
} vec2;

typedef struct {
    float v[16];
} mat4;

typedef struct{
    mat4 proj;
    mat4 view;
    VkDeviceAddress SpriteDrawBufferPtr;
} PushConstants;

typedef struct {
    mat4 transform;
} SpriteDrawCommand;

static PushConstants pcs;

#define MAX_SPRITE_COUNT 16

mat4 ortho2D(float width, float height){
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

bool platform_resize_window_callback(){
    if(!swapchain) return true;
    VkResult result = vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    if(result != VK_SUCCESS){
        printf("ERROR: Couldn't wait for fences\n");
        return false;
    }

    for(int i = 0; i < swapchainImageViews.count; i++){
        vkDestroyImageView(device,swapchainImageViews.items[i],NULL);
    }
    swapchainImages.count = 0;
    swapchainImageViews.count = 0;
    vkDestroySwapchainKHR(device, swapchain, NULL);

    if(!initSwapchain()) return false;

    pcs.proj = ortho2D(swapchainExtent.width, swapchainExtent.height);
    pcs.view = (mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        -((float)swapchainExtent.width)/2,-((float)swapchainExtent.height)/2,0,1,
    };

    return true;
}

int main(){
    platform_create_window("TRIEX",640,480);

    if(!initialize_vulkan()) return 1;
    if(!createSurface()) return 1;
    if(!getDevice()) return 1;
    if(!initSwapchain()) return 1;
    if(!initCommandPool()) return 1;
    if(!initCommandBuffer()) return 1;
    if(!createAllNeededSyncrhonizationObjects()) return 1;

    String_Builder sb = {0};
    nob_read_entire_file("assets/sprite.vert",&sb);
    sb_append_null(&sb);
    
    VkShaderModule vertexShader;
    if(!compileShader(sb.items, shaderc_vertex_shader,&vertexShader)) return 1;

    sb.count = 0;
    nob_read_entire_file("assets/sprite.frag",&sb);
    sb_append_null(&sb);
    
    VkShaderModule fragmentShader;
    if(!compileShader(sb.items, shaderc_fragment_shader,&fragmentShader)) return 1;

    if(!initGraphicsPipeline(vertexShader,fragmentShader, sizeof(PushConstants))) return 1;

    pcs.proj = ortho2D(swapchainExtent.width, swapchainExtent.height);
    pcs.view = (mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        -((float)swapchainExtent.width)/2,-((float)swapchainExtent.height)/2,0,1,
    };

    VkBuffer spriteDrawBuffer;
    VkDeviceMemory spriteDrawMemory;
    if(!createBuffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,sizeof(SpriteDrawCommand)*MAX_SPRITE_COUNT,&spriteDrawBuffer,&spriteDrawMemory)) return 1;

    VkBufferDeviceAddressInfoKHR addrInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR,
        .buffer = spriteDrawBuffer,
    };
    pcs.SpriteDrawBufferPtr = vkGetBufferDeviceAddress(device, &addrInfo);

    uint32_t imageIndex;

    uint64_t oldTime = platform_get_time();

    size_t targetFPS = 120;
    float frameDuration = 1.0f / targetFPS;

    SpriteDrawCommand instanceData[MAX_SPRITE_COUNT] = {0};
    instanceData[0] = (SpriteDrawCommand){
        .transform = (mat4){
            200,0,0,0,
            0,200,0,0,
            0,0,1,0,
            0,0,0,1,
        }
    };

    transferDataToMemory(spriteDrawMemory,&instanceData,0,sizeof(instanceData));

    while(platform_still_running()){
        if(!platform_window_handle_events()) return 1;

        uint64_t time = platform_get_time();
        float deltaTime = (float)(time - oldTime) / 1000.0f;
        oldTime = time;

        instanceData[1] = (SpriteDrawCommand){
            .transform = (mat4){
                200,0,0,0,
                0,200,0,0,
                0,0,1,0,
                swapchainExtent.width / 2 - 100,swapchainExtent.height / 2 - 100,0,1,
            }
        };

        transferDataToMemory(spriteDrawMemory,&instanceData[1],sizeof(instanceData[0]),sizeof(instanceData[0]));

        vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &inFlightFence);

        vkResetCommandBuffer(cmd, 0);

        vkAcquireNextImageKHR(device,swapchain,UINT64_MAX,imageAvailableSemaphore,NULL, &imageIndex);
    
        VkCommandBufferBeginInfo commandBufferBeginInfo = {0};
        commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBeginInfo.pNext = NULL;
        commandBufferBeginInfo.flags = 0;
        commandBufferBeginInfo.pInheritanceInfo = NULL;
        vkBeginCommandBuffer(cmd,&commandBufferBeginInfo);

        // Transition image layout from undefined to color attachment optimal
        VkImageMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapchainImages.items[imageIndex];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, NULL,
            0, NULL,
            1, &barrier
        );
        
        VkRenderingAttachmentInfo colorAttachment = {0};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = swapchainImageViews.items[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color.float32[0] = 0.0f;
        colorAttachment.clearValue.color.float32[1] = 0.0f;
        colorAttachment.clearValue.color.float32[2] = 0.0f;
        colorAttachment.clearValue.color.float32[3] = 1.0f;

        VkRenderingInfo renderingInfo = {0};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        VkOffset2D offset = {0};
        renderingInfo.renderArea.offset = offset;
        renderingInfo.renderArea.extent = swapchainExtent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = NULL;
        renderingInfo.pStencilAttachment = NULL;

        vkCmdBeginRendering(cmd, &renderingInfo);

        VkViewport viewport = {0};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = swapchainExtent.width;
        viewport.height = swapchainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        
        VkRect2D scissor = {0};
        scissor.offset = offset;
        scissor.extent = swapchainExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkDeviceSize vOffset = 0;

        vkCmdPushConstants(cmd,pipelineLayout,VK_SHADER_STAGE_ALL,0,sizeof(PushConstants), &pcs);

        vkCmdDraw(cmd,6,2,0,0);


        vkCmdEndRendering(cmd);

        // Transition image layout from color attachment optimal to present src
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0, NULL,
            0, NULL,
            1, &barrier
        );

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo = {0};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        
        VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;
        
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence);
        
        VkPresentInfoKHR presentInfo = {0};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        
        VkSwapchainKHR swapChains[] = {swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;
        vkQueuePresentKHR(presentQueue, &presentInfo);

        uint64_t frameTook = platform_get_time() - time;
        if(((float)(frameTook)/1000.0f) < frameDuration) platform_sleep(frameDuration*1000.0f - frameTook);
    }

    return 0;
}