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
#include "modules/font_freetype.h"
#include "ffmpeg_video.h"
#include "gui_helpers.h"

typedef struct{
    mat4 projView;
    VkDeviceAddress SpriteDrawBufferPtr;
} PushConstants;

typedef struct{
    mat4 projView;
    mat4 model;
} PushConstantsPreview;

static PushConstants pcs;
static PushConstantsPreview pcsPreview;

bool afterResize(){
    mat4 ortho = ortho2D(swapchainExtent.width, swapchainExtent.height);
    mat4 projView = mat4mul(&ortho, &(mat4){
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        -((float)swapchainExtent.width)/2,-((float)swapchainExtent.height)/2,0,1,
    });

    pcs.projView = projView;
    pcsPreview.projView = projView;

    return true;
}

static VkPipeline pipeline;
static VkPipelineLayout pipelineLayout;

static VkPipeline pipelinePreview;
static VkPipelineLayout pipelineLayoutPreview;
static VkDescriptorSet descriptorSet;
static VkDescriptorSetLayout descriptorSetLayout;

size_t imageWidth, imageHeight;

GlyphAtlas atlas = {0};

#include "engine/platform.h"

uint64_t TIMER;
uint64_t TIMER_TOTAL;
#define CHECK_TIMER(thing) do {uint64_t newTimer = platform_get_time();printf("%s: took %.2fs\n", (thing), (float)(newTimer - TIMER) / 1000.0f);TIMER = newTimer;} while(0)
#define CHECK_TIMER_TOTAL(thing) do {uint64_t newTimer = platform_get_time();printf("%s: took %.2fs\n", (thing), (float)(newTimer - TIMER_TOTAL) / 1000.0f);TIMER_TOTAL = newTimer;} while(0)

int main(int argc, char** argv){
    if(argc < 2){
        printf("you need to provide filename\n");
        return 1;
    }
    TIMER = platform_get_time();
    TIMER_TOTAL = TIMER;
    if(!engineInit("FVFX", 640,480)) return 1;
    CHECK_TIMER("init engine");

    {
        afterResize();

        // ------------------ sprite manager initialization ------------------

        File_Paths initialTextures = {0};
        da_append(&initialTextures,"assets/Jimbo100x.png");
        if(!initBindlessTextures(initialTextures)) return 1;

        VkImage fontImage;
        VkDeviceMemory fontMemory;
        VkImageView fontImageView;

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

        CHECK_TIMER("init sprite");

        // ------------------          INIT SDF            ------------------

        if(!GetFontSDFAtlas("assets/font/VictorMono-Regular.ttf",&fontImage, &fontMemory, &fontImageView, &atlas)) return false;

        addBindlessTextureRaw((Texture){
            .name = "font",
            .width = atlas.width,
            .height = atlas.height,
            .image = fontImage,
            .memory = fontMemory,
            .imageView = fontImageView,
        });

        CHECK_TIMER("init sdf");

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

        if(!ffmpegInit(argv[1],&image,&imageMemory, &imageView, &imageWidth, &imageHeight)) {
            printf("Couldn't load video preview frame\n");
            return 1;
        }

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

        CHECK_TIMER("init video");
    }

    CHECK_TIMER_TOTAL("init");
    return engineStart();
}

float time;

bool update(float deltaTime){
    Rect previewPos = (Rect){
        .width = swapchainExtent.width,
        .height = (float)swapchainExtent.height*3/4,
        .y = 30,
    };

    Rect previewRect = fitRectangle(previewPos, imageWidth, imageHeight);

    float backgroundSpeed = cosf(sinf(time/20))*time;

    //Draw Black stuff behind 
    drawSprite((SpriteDrawCommand){
        .position = (vec2){previewPos.x,previewPos.y},
        .scale = (vec2){previewPos.width, previewPos.height},
        .textureIDEffects = getTextureID("assets/Jimbo100x.png"),
        .offset = (vec2){time,time/2},
        .size = (vec2){1.0f+(sinf(backgroundSpeed)/2+0.5)*7,1.0f+(sinf(backgroundSpeed)/2+0.5)*7},
    });

    Rect timelineRect = (Rect){
        .width = swapchainExtent.width,
        .height = (float)swapchainExtent.height - (previewPos.y+previewPos.height),
        .y = previewPos.y+previewPos.height,
    };

    if(!ffmpegProcessFrame()) return 1;

    float percent = getFrameTime()/getDuration();

    float cursorWidth = timelineRect.width / 500;
    
    drawSprite((SpriteDrawCommand){
        .position = (vec2){floor(timelineRect.x+percent*timelineRect.width+cursorWidth/2),timelineRect.y},
        .scale = (vec2){cursorWidth, timelineRect.height},
        .albedo = (vec3){1.0,0.0,0.0},
    });

    pcsPreview.model = (mat4){
        previewRect.width,0,0,0,
        0,previewRect.height,0,0,
        0,0,1,0,
        previewRect.x,previewRect.y,0,1,
    };

    drawText("FVFX", 0xFFFFFF, 16, (Rect){
        .x = 10,
        .y = 3,
    });

    char text[256];
    float textFont = 16;

    sprintf(text, "%.2fs/%.2fs", getFrameTime(), getDuration());

    drawText(text, 0xFFFFFF, textFont, (Rect){
        .x = swapchainExtent.width / 2 - measureText(text,textFont) / 2,
        .y = 3,
    });


    time += deltaTime;
    return true;
}

bool draw(){
    //sprite pass
    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = getSwapchainImageView(),
        .clearColor = (Color){18.0f/255.f,18.0f/255.f,18.0f/255.f,1.0f},
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