#include <stdio.h>
#include <stdbool.h>

#define NOB_STRIP_PREFIX
#include "nob.h"

#include "vulkan/vulkan.h"

#include "engine/engine.h"
#include "engine/app.h"
#include "engine/vulkan_globals.h"
#include "engine/vulkan_createGraphicPipelines.h"
#include "engine/vulkan_compileShader.h"
#include "engine/vulkan_helpers.h"
#include "engine/vulkan_buffer.h"

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

bool afterResize(){
    pcs.proj = ortho2D(swapchainExtent.width, swapchainExtent.height);
    pcs.view = (mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        -((float)swapchainExtent.width)/2,-((float)swapchainExtent.height)/2,0,1,
    };

    return true;
}

static String_Builder sb;
static SpriteDrawCommand instanceData[MAX_SPRITE_COUNT] = {0};
static VkPipeline pipeline;
static VkPipelineLayout pipelineLayout;
static VkBuffer spriteDrawBuffer;
static VkDeviceMemory spriteDrawMemory;

int main(){
    if(!engineInit("FVFX", 640,480)) return 1;

    nob_read_entire_file("assets/sprite.vert",&sb);
    sb_append_null(&sb);
    
    VkShaderModule vertexShader;
    if(!compileShader(sb.items, shaderc_vertex_shader,&vertexShader)) return false;
    
    sb.count = 0;
    nob_read_entire_file("assets/sprite.frag",&sb);
    sb_append_null(&sb);
    
    VkShaderModule fragmentShader;
    if(!compileShader(sb.items, shaderc_fragment_shader,&fragmentShader)) return false;
    
    if(!createGraphicPipeline(vertexShader,fragmentShader, sizeof(PushConstants), &pipeline, &pipelineLayout)) return false;
    
    pcs.proj = ortho2D(swapchainExtent.width, swapchainExtent.height);
    pcs.view = (mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        -((float)swapchainExtent.width)/2,-((float)swapchainExtent.height)/2,0,1,
    };
    
    if(!createBuffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,sizeof(SpriteDrawCommand)*MAX_SPRITE_COUNT,&spriteDrawBuffer,&spriteDrawMemory)) return false;
    
    VkBufferDeviceAddressInfoKHR addrInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR,
        .buffer = spriteDrawBuffer,
    };
    pcs.SpriteDrawBufferPtr = vkGetBufferDeviceAddress(device, &addrInfo);
    
    instanceData[0] = (SpriteDrawCommand){
        .transform = (mat4){
            200,0,0,0,
            0,200,0,0,
            0,0,1,0,
            0,0,0,1,
        }
    };
    
    transferDataToMemory(spriteDrawMemory,&instanceData,0,sizeof(instanceData));

    return engineStart();
}

bool update(float deltaTime){
    instanceData[1] = (SpriteDrawCommand){
        .transform = (mat4){
            200,0,0,0,
            0,200,0,0,
            0,0,1,0,
            swapchainExtent.width / 2 - 100,swapchainExtent.height / 2 - 100,0,1,
        }
    };

    transferDataToMemory(spriteDrawMemory,&instanceData[1],sizeof(instanceData[0]),sizeof(instanceData[0]));

    return true;
}

bool draw(){
    VkRenderingAttachmentInfo colorAttachment = {0};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = getSwapchainImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color.float32[0] = 0.0f;
    colorAttachment.clearValue.color.float32[1] = 0.0f;
    colorAttachment.clearValue.color.float32[2] = 0.0f;
    colorAttachment.clearValue.color.float32[3] = 1.0f;

    VkRenderingInfo renderingInfo = {0};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = (VkOffset2D){0};
    renderingInfo.renderArea.extent = swapchainExtent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = NULL;
    renderingInfo.pStencilAttachment = NULL;

    vkCmdBeginRendering(cmd, &renderingInfo);

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = swapchainExtent.width,
        .height = swapchainExtent.height
    });
        
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = swapchainExtent,
    });
        
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdPushConstants(cmd,pipelineLayout,VK_SHADER_STAGE_ALL,0,sizeof(PushConstants), &pcs);

    vkCmdDraw(cmd,6,2,0,0);

    vkCmdEndRendering(cmd);

    return true;
}