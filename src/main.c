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
#include "engine/vulkan_images.h"

#include "3rdparty/stb_image.h"
#include "math.h"

typedef struct {
    float x;
    float y;
} vec2;

typedef struct {
    float x;
    float y;
    float z;
} vec3;

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
    uint32_t textureID;
    vec3 albedo;
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

static SpriteDrawCommand instanceData[MAX_SPRITE_COUNT] = {0};
static VkPipeline pipeline;
static VkPipelineLayout pipelineLayout;
static VkBuffer spriteDrawBuffer;
static VkDeviceMemory spriteDrawMemory;

typedef struct{
    char* data;
} STB_Image;

typedef struct {
    STB_Image* items;
    size_t count;
    size_t capacity;
} STB_Images;

STB_Images images = {0};
float time;

size_t imageIndex = 0;

char* mapped;
int width, height;

int main(){
    if(!engineInit("FVFX", 640,480)) return 1;

    {
        String_Builder sb = {0};
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
        
        instanceData[1] = (SpriteDrawCommand){
            .transform = (mat4){
                200,0,0,0,
                0,200,0,0,
                0,0,1,0,
                0,0,0,1,
            },
            .textureID = -1,
            .albedo = (vec3){1.0f,0.0,0.5},
        };
        
        transferDataToMemory(spriteDrawMemory,&instanceData,0,sizeof(instanceData));
    
        File_Paths children = {0};
    
        nob_read_entire_dir("assets/video",&children);
    
        sb.count = 0;
        sb_append_cstr(&sb, "assets/video/");
        size_t savedCount = sb.count;
    
        for(int i = 0; i < children.count; i++){
            if(!sv_end_with(sv_from_cstr(children.items[i]),".png")) continue;
    
            sb.count = savedCount;
            sb_append_cstr(&sb,children.items[i]);
            sb_append_null(&sb);
    
            STB_Image img = {0};
    
            img.data = (char*)stbi_load(sb.items,&width,&height, NULL, 4);
            if(img.data == NULL){
                printf("ERROR: Couldn't read image from disk (%s)\n", children.items[i]);
                return 1;
            }
    
            da_append(&images,img);
        }

        da_free(children);
    
        VkImage image;
        VkDeviceMemory imageMemory;
        VkImageView imageView;
    
        if(!createImage(width,height,VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &image, &imageMemory)){
            return 1;
        }
    
        if(!sendDataToImage(image,images.items[imageIndex].data,width,width*sizeof(uint32_t),height)){
            return 1;
        }
    
        if(!createImageView(image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, &imageView)){
            return 1;
        }
    
        VkDescriptorImageInfo descriptorImageInfo = {0};
        descriptorImageInfo.sampler = samplerLinear;
        descriptorImageInfo.imageView = imageView;
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
        VkWriteDescriptorSet writeDescriptorSet = {0};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.pNext = NULL;
        writeDescriptorSet.dstSet = bindlessDescriptorSet;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.pImageInfo = &descriptorImageInfo;
    
        vkUpdateDescriptorSets(device,1,&writeDescriptorSet,0,NULL);
    
        VkResult result = vkMapMemory(device,imageMemory,0,width*height*sizeof(uint32_t),0,(void**)&mapped);
        if(result != VK_SUCCESS){
            return 1;
        }

        da_free(sb);
    }

    return engineStart();
}

bool update(float deltaTime){
    float aspectRatio = (float)width / (float)height;

    float uwidth = swapchainExtent.height * aspectRatio;
    float uheight = swapchainExtent.height;

    instanceData[0] = (SpriteDrawCommand){
        .transform = (mat4){
            uwidth,0,0,0,
            0,uheight,0,0,
            0,0,1,0,
            swapchainExtent.width / 2 - uwidth / 2,swapchainExtent.height / 2 -uheight / 2,0,1,
        },
        .textureID = 0,
    };

    transferDataToMemory(spriteDrawMemory,&instanceData[0],0,sizeof(instanceData[0]));

    time += deltaTime;

    size_t currentIndex = (size_t)(floorf(time * 60)) % (images.count-1);

    if(currentIndex != imageIndex){
        imageIndex = currentIndex;

        memcpy(mapped,images.items[imageIndex].data, width*height*sizeof(uint32_t));
    }

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
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&bindlessDescriptorSet,0,NULL);

    vkCmdPushConstants(cmd,pipelineLayout,VK_SHADER_STAGE_ALL,0,sizeof(PushConstants), &pcs);

    vkCmdDraw(cmd,6,2,0,0);

    vkCmdEndRendering(cmd);

    return true;
}