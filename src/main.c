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
#include "engine/input.h"

#include "math.h"
#include "modules/gmath.h"
#include "modules/bindlessTexturesManager.h"
#include "modules/spriteManager.h"
#include "ffmpeg_video.h"

typedef struct{
    mat4 proj;
    mat4 view;
    VkDeviceAddress SpriteDrawBufferPtr;
} PushConstants;

typedef struct{
    mat4 proj;
    mat4 view;
    mat4 model;
} PushConstantsPreview;

static PushConstants pcs;

static PushConstantsPreview pcsPreview;

bool afterResize(){
    pcs.proj = ortho2D(swapchainExtent.width, swapchainExtent.height);
    pcs.view = (mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        -((float)swapchainExtent.width)/2,-((float)swapchainExtent.height)/2,0,1,
    };

    pcsPreview.proj = ortho2D(swapchainExtent.width, swapchainExtent.height);
    pcsPreview.view = (mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        -((float)swapchainExtent.width)/2,-((float)swapchainExtent.height)/2,0,1,
    };

    return true;
}

static VkPipeline pipeline;
static VkPipelineLayout pipelineLayout;

static VkPipeline pipelinePreview;
static VkPipelineLayout pipelineLayoutPreview;
static VkDescriptorSet descriptorSet;
static VkDescriptorSetLayout descriptorSetLayout;

size_t lenaTextureID;

size_t imageWidth, imageHeight;

int main(int argc, char** argv){
    if(!engineInit("FVFX", 640,480)) return 1;

    if(argc < 2){
        printf("you need to provide filename\n");
        return 1;
    }

    {
        afterResize();

        // ------------------ sprite manager initialization ------------------

        File_Paths initialTextures = {0};
        da_append(&initialTextures,"assets/test.png");
        if(!initBindlessTextures(initialTextures)) return 1;
        lenaTextureID = getTextureID("assets/test.png");

        String_Builder sb = {0};
        nob_read_entire_file("assets/shaders/compiled/sprite.vert.spv",&sb);
        sb_append_null(&sb);
        
        VkShaderModule vertexShader;
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&vertexShader)) return false;
        
        sb.count = 0;
        nob_read_entire_file("assets/shaders/compiled/sprite.frag.spv",&sb);
        sb_append_null(&sb);
        
        VkShaderModule fragmentShader;
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&fragmentShader)) return false;
        
        if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
            .vertexShader = vertexShader,
            .fragmentShader = fragmentShader,
            .pushConstantsSize = sizeof(PushConstants),
            .pipelineOUT = &pipeline, 
            .pipelineLayoutOUT = &pipelineLayout,
            .descriptorSetLayoutCount = 1,
            .descriptorSetLayouts = &bindlessDescriptorSetLayout,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        })) return false;
        
        if(!initSpriteManager()) return 1;
        pcs.SpriteDrawBufferPtr = vkGetBufferDeviceAddressEX(spriteDrawBuffer);

        // ------------------ video preview initialization ------------------

        sb.count = 0;
        nob_read_entire_file("assets/shaders/compiled/normal_texture.vert.spv",&sb);
        sb_append_null(&sb);
        
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&vertexShader)) return false;
        
        sb.count = 0;
        nob_read_entire_file("assets/shaders/compiled/normal_texture.frag.spv",&sb);
        sb_append_null(&sb);
        
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&fragmentShader)) return false;

        VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {0};
        descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorSetLayoutBinding.descriptorCount = 1;
        descriptorSetLayoutBinding.binding = 0;
        descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {0};
        descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCreateInfo.bindingCount  = 1;
        descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;

        if(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &descriptorSetLayout) != VK_SUCCESS){
            printf("ERROR\n");
            return 1;
        }

        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;

        vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, &descriptorSet);
        
        if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
            .vertexShader = vertexShader,
            .fragmentShader = fragmentShader,
            .pushConstantsSize = sizeof(PushConstantsPreview),
            .pipelineOUT = &pipelinePreview, 
            .pipelineLayoutOUT = &pipelineLayoutPreview,
            .descriptorSetLayoutCount = 1,
            .descriptorSetLayouts = &descriptorSetLayout,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        })) return false;

        da_free(sb);

        VkImage image;
        VkImageView imageView;
        VkDeviceMemory imageMemory;

        if(!loadFrame(argv[1],&image,&imageMemory, &imageView, &imageWidth, &imageHeight)) return 1;

        VkDescriptorImageInfo descriptorImageInfo = {0};
        descriptorImageInfo.sampler = samplerLinear;
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorImageInfo.imageView = imageView;

        VkWriteDescriptorSet writeDescriptorSet = {0};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.pImageInfo = &descriptorImageInfo;

        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);
    }

    return engineStart();
}

float time;

vec2 pos = (vec2){300, 50};
vec2 acc = (vec2){5.0f,-2.0};
vec2 size = (vec2){200,200};

size_t jimboTextureID = -1;

vec2 pos2 = {0};

bool update(float deltaTime){
    time += deltaTime;

    acc.y += deltaTime * 9.8f;
    
    if(pos.x + size.x + acc.x < swapchainExtent.width && pos.x + acc.x > 0){
        pos.x += acc.x;
    }else{
        acc.x = acc.x * -1.0f;
    }

    if(pos.y + size.y + acc.y < swapchainExtent.height){
        pos.y += acc.y;
    }else{
        acc.y = acc.y * -0.96f;
    }

    if(input.keys[KEY_RIGHT].isDown) pos2.x += deltaTime * 200;
    if(input.keys[KEY_LEFT].isDown) pos2.x -= deltaTime * 200;
    if(input.keys[KEY_DOWN].isDown) pos2.y += deltaTime * 200;
    if(input.keys[KEY_UP].isDown) pos2.y -= deltaTime * 200;

    drawSprite((SpriteDrawCommand){
        .transform = (mat4){
            200,0,0,0,
            0,200,0,0,
            0,0,1,0,
            pos2.x,pos2.y,0,1,
        },
        .textureID = -1,
        .albedo = (vec3){0.0,1.0,0.0},
    });

    drawSprite((SpriteDrawCommand){
        .transform = (mat4){
            size.x,0,0,0,
            0,size.y,0,0,
            0,0,1,0,
            pos.x,pos.y,0,1,
        },
        .textureID = lenaTextureID,
        .albedo = (vec3){1.0,0.0,0.0},
    });

    float winW = (float)swapchainExtent.width;
    float winH = (float)swapchainExtent.height;
    float imgAspect = (float)imageWidth / (float)imageHeight;
    float winAspect = winW / winH;

    if (winAspect < imgAspect) {
        float scaledH = winW / imgAspect;
        float yOffset = (winH - scaledH) * 0.5f;
        
        pcsPreview.model = (mat4){
            winW,   0,       0, 0,
            0,      scaledH, 0, 0,
            0,      0,       1, 0,
            0,      yOffset, 0, 1
        };
    } else {
        float scaledW = winH * imgAspect;
        float xOffset = (winW - scaledW) * 0.5f;
        
        pcsPreview.model = (mat4){
            scaledW, 0,      0, 0,
            0,       winH,   0, 0,
            0,       0,      1, 0,
            xOffset, 0,      0, 1
        };
    }


    return true;
}

bool draw(){
    //sprite pass
    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = getSwapchainImageView(),
        .clearColor = (Color){18.0f/255.f,18.0f/255.f,18.0f/255.f,1.0f}
    });
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

    renderSprites();

    vkCmdEndRendering(cmd);

    //preview pass
    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = getSwapchainImageView(),
        .doNotClearColor = true,
    });

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = swapchainExtent.width,
        .height = swapchainExtent.height
    });
        
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = swapchainExtent,
    });

    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinePreview);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayoutPreview,0,1,&descriptorSet,0,NULL);
    vkCmdPushConstants(cmd,pipelineLayoutPreview,VK_SHADER_STAGE_ALL,0,sizeof(PushConstantsPreview), &pcsPreview);

    vkCmdDraw(cmd, 6, 1, 0, 0);

    vkCmdEndRendering(cmd);

    return true;
}