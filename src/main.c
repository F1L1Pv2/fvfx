#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

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
#include "ffmpeg_media.h"
#include "ffmpeg_media_render.h"
#include "gui_helpers.h"
#include <stdatomic.h>
#include "sound_engine.h"
#include "ffmpeg_worker.h"
#include "vfx.h"
#include "ui_effectsRack.h"
#include "ui_topBar.h"
#include "ui_timeline.h"
#include "ui_splitters.h"

#ifndef min
#define min(a,b) ((a) > (b) ? (b) : (a))
#endif

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

static VkPipeline pipelineImagePreview;
static VkPipelineLayout pipelineImageLayoutPreview;
static VkDescriptorSet previewDescriptorSet;

static VkPipeline pipelinePreview;
static VkPipelineLayout pipelineLayoutPreview;
static VkDescriptorSet outDescriptorSet1;
static VkDescriptorSet outDescriptorSet2;
VkDescriptorSetLayout vfxDescriptorSetLayout;

VkImage previewImage1;
VkDeviceMemory previewMemory1;
VkImageView previewView1;
void* previewMemoryMapped1;

VkImage previewImage2;
VkDeviceMemory previewMemory2;
VkImageView previewView2;
void* previewMemoryMapped2;


GlyphAtlas atlas = {0};

#include "engine/platform.h"

uint64_t TIMER;
uint64_t TIMER_TOTAL;
#define CHECK_TIMER(thing) do {uint64_t newTimer = platform_get_time();printf("%s: took %.2fs\n", (thing), (float)(newTimer - TIMER) / 1000.0f);TIMER = newTimer;} while(0)
#define CHECK_TIMER_TOTAL(thing) do {uint64_t newTimer = platform_get_time();printf("%s: took %.2fs\n", (thing), (float)(newTimer - TIMER_TOTAL) / 1000.0f);TIMER_TOTAL = newTimer;} while(0)

MediaRenderContext renderContext = {0};
Media media = {0};
void* videoMapped = NULL;
size_t videoVulkanStride = 0;

Hashmap vfxModulesHashMap = {0};
VfxInstances currentModuleInstances = {0};

VkShaderModule vfxVertexShader;

double Time;
atomic_bool playing = true;
bool oldPlaying = false;

int main(int argc, char** argv){
    if(argc < 2){
        printf("you need to provide filename\n");
        return 1;
    }
    TIMER = platform_get_time();
    TIMER_TOTAL = TIMER;
    if(!engineInit("FVFX", 640,480)) return 1;
    CHECK_TIMER("init engine");

    initHashMap(&vfxModulesHashMap, 100);

    String_Builder sb = {0};
    {
        afterResize();

        // ------------------ sprite manager initialization ------------------

        File_Paths initialTextures = {0};
        da_append(&initialTextures,"assets/DropdownDelete.png");
        da_append(&initialTextures,"assets/DropdownOpen.png");
        da_append(&initialTextures,"assets/DropdownClosed.png");
        da_append(&initialTextures,"assets/ColorPicker.png");
        if(!initBindlessTextures(initialTextures)) return 1;

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

        VkImage fontImage = NULL;
        VkDeviceMemory fontMemory = NULL;
        VkImageView fontImageView = NULL;

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

        if(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &vfxDescriptorSetLayout) != VK_SUCCESS){
            printf("ERROR\n");
            return 1;
        }

        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        descriptorSetAllocateInfo.pSetLayouts = &vfxDescriptorSetLayout;

        vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, &outDescriptorSet1);
        vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, &outDescriptorSet2);
        
        if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
            .vertexShader = vertexShader,
            .fragmentShader = fragmentShader,
            .pushConstantsSize = sizeof(PushConstantsPreview),
            .pipelineOUT = &pipelinePreview, 
            .pipelineLayoutOUT = &pipelineLayoutPreview,
            .descriptorSetLayoutCount = 1,
            .descriptorSetLayouts = &vfxDescriptorSetLayout,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        })) return false;

        VkImage image;
        VkImageView imageView;
        VkDeviceMemory imageMemory;

        if(!ffmpegMediaInit(argv[1], &media)) {
            printf("Couldn't load video\n");
            return 1;
        }

        if(media.audioStreamIndex != -1){
            initSoundEngine();
        }

        if(!createImage(media.videoCodecContext->width,media.videoCodecContext->height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &image,&imageMemory)){
            printf("Couldn't create video image\n");
            return 1;
        }

        {
            VkCommandBuffer tempCmd = beginSingleTimeCommands();
            VkImageMemoryBarrier barrier = {0};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            
            vkCmdPipelineBarrier(
                tempCmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, NULL,
                0, NULL,
                1, &barrier
            );

            endSingleTimeCommands(tempCmd);
        }

        if(!createImageView(image,VK_FORMAT_R8G8B8A8_UNORM, 
                        VK_IMAGE_ASPECT_COLOR_BIT, &imageView)){
            printf("Couldn't create video image view\n");
            return 1;
        }

        VkImageSubresource subresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .arrayLayer = 0
        };

        VkSubresourceLayout layout;
        vkGetImageSubresourceLayout(device, image, &subresource, &layout);
        videoVulkanStride = layout.rowPitch;

        vkMapMemory(device,imageMemory, 0, videoVulkanStride*media.videoCodecContext->height, 0, &videoMapped);

        CHECK_TIMER("init video");

        //initializing preview image (vfx pipeline stuff)
        sb.count = 0;
        nob_read_entire_file("assets/shaders/compiled/vfx_texture.vert.spv",&sb);
        sb_append_null(&sb);
        
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&vertexShader)) return false;

        vfxVertexShader = vertexShader;
        
        sb.count = 0;
        nob_read_entire_file("assets/shaders/compiled/vfx_texture.frag.spv",&sb);
        sb_append_null(&sb);
        
        if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count-1,&fragmentShader)) return false;

        // VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        descriptorSetAllocateInfo.pSetLayouts = &vfxDescriptorSetLayout;

        vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, &previewDescriptorSet);
        
        if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
            .vertexShader = vertexShader,
            .fragmentShader = fragmentShader,
            .pipelineOUT = &pipelineImagePreview, 
            .pipelineLayoutOUT = &pipelineImageLayoutPreview,
            .descriptorSetLayoutCount = 1,
            .descriptorSetLayouts = &vfxDescriptorSetLayout,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        })) return false;

        // ----- binding ffmpeg video into preview image

        VkDescriptorImageInfo descriptorImageInfo = {0};
        VkWriteDescriptorSet writeDescriptorSet = {0};

        descriptorImageInfo.sampler = samplerLinear;
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorImageInfo.imageView = imageView;

        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.dstSet = previewDescriptorSet;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.pImageInfo = &descriptorImageInfo;

        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);

        if(!createImage(media.videoCodecContext->width,media.videoCodecContext->height, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_LINEAR,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &previewImage1,&previewMemory1)){
            printf("Couldn't create preview image\n");
            return 1;
        }

        if(!createImageView(previewImage1,VK_FORMAT_B8G8R8A8_UNORM, 
                        VK_IMAGE_ASPECT_COLOR_BIT, &previewView1)){
            printf("Couldn't create preview image view\n");
            return 1;
        }

        if(!createImage(media.videoCodecContext->width,media.videoCodecContext->height, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_LINEAR,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &previewImage2,&previewMemory2)){
            printf("Couldn't create preview image\n");
            return 1;
        }

        if(!createImageView(previewImage2,VK_FORMAT_B8G8R8A8_UNORM, 
                        VK_IMAGE_ASPECT_COLOR_BIT, &previewView2)){
            printf("Couldn't create preview image view\n");
            return 1;
        }

        vkMapMemory(device,previewMemory1, 0, videoVulkanStride*media.videoCodecContext->height, 0,&previewMemoryMapped1);
        vkMapMemory(device,previewMemory2, 0, videoVulkanStride*media.videoCodecContext->height, 0,&previewMemoryMapped2);

        descriptorImageInfo.sampler = samplerLinear;
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorImageInfo.imageView = previewView1;

        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.dstSet = outDescriptorSet1;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.pImageInfo = &descriptorImageInfo;

        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);

        descriptorImageInfo.sampler = samplerLinear;
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorImageInfo.imageView = previewView2;

        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.dstSet = outDescriptorSet2;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.pImageInfo = &descriptorImageInfo;

        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);

        CHECK_TIMER("init vfx pipeline");

        initializeFFmpegWorker(&media);
    }

    sb_free(sb);

    CHECK_TIMER_TOTAL("init");
    return engineStart();
}

String_Builder sb = {0};

float effectTabSplitterOffset = 150;
float timelineSplitterOffset = 100;

#define SPLITTER_THICKNESS 4

bool rendering = false;
bool stopRendering = false;

void* frameDataProcessed = NULL;

void updatePreviewFrame(){
    Frame* frame = NULL;

    while ((frame = workerPeekVideoFrame()) != NULL) {
        if (frame->frameTime > Time)
            break;
        frame = workerAskForVideoFrame();
        if (frame) {
            for (int i = 0; i < media.videoCodecContext->height; i++) {
                memcpy(
                    videoMapped + videoVulkanStride * i,
                    frame->video.data + media.videoCodecContext->width * sizeof(uint32_t) * i,
                    media.videoCodecContext->width * sizeof(uint32_t)
                );
            }
            free(frame->video.data);
            frame->video.data = NULL;
        }
    }

    if (Time >= media.duration) {
        if (rendering)
            stopRendering = true;
        Time = 0;
        workerAskForSeek(Time);
    }
}

bool handleDragNDrop(Rect effectsTab){
    if(platform_drag_and_drop_available()){
        ui_reset();

        int count = -1;
        const char** dragndrop = platform_get_drag_and_drop_files(&count);

        printf("Got drag and drop mousex: %zu mousey: %zu\n", input.mouse_x, input.mouse_y);
        if(pointInsideRect(input.mouse_x, input.mouse_y, effectsTab)){
            if(!addEffectsToRack(&currentModuleInstances, &vfxModulesHashMap, dragndrop, count)) return false;
        }
        
        platform_release_drag_and_drop(dragndrop, count);
    }
    return true;
}

bool update(float deltaTime){
    ui_begin();

    temp_reset();
    if(!soundEngineInitialized() && playing) Time += deltaTime;

    if (playing) updatePreviewFrame();

    if(input.keys[KEY_SPACE].justPressed) playing = !playing;

    if(oldPlaying != playing){
        if(playing){
            workerAskForResume();
        }else{
            workerAskForPause();
        }
        oldPlaying = playing;
    }

    if(input.keys[KEY_SHIFT].isDown && input.scroll != 0){
        UI_FONT_SIZE += input.scroll*deltaTime;
    }

    if(input.keys[KEY_R].justReleased){
        if(!rendering) {
            rendering = true;
            stopRendering = false;

            Time = 0;
            workerAskForSeek(Time);

            TODO("implement this");

            // ffmpegMediaRenderInit(&media, "output.mp4", &renderContext);
            // frameDataProcessed = calloc(media.videoCodecContext->width*media.videoCodecContext->height*sizeof(uint32_t),1);
        }
    }

    Rect topBarRect = (Rect){
        .x = 0,
        .y = 0,
        .width = swapchainExtent.width,
        .height = 30,
    };

    Rect effectsTab = (Rect){
        .width = effectTabSplitterOffset,
        .height = swapchainExtent.height - topBarRect.height,
        .y = topBarRect.y+topBarRect.height,
        .x = swapchainExtent.width - effectTabSplitterOffset,
    };

    Rect timelineRect = (Rect){
        .height = timelineSplitterOffset,
    };

    Rect previewPos = (Rect){
        .width = swapchainExtent.width - effectsTab.width,
        .height = swapchainExtent.height - timelineRect.height - topBarRect.height,
        .y = topBarRect.y+topBarRect.height,
    };

    timelineRect.width = swapchainExtent.width - effectsTab.width;
    timelineRect.y = previewPos.y + previewPos.height;

    Rect previewRect = fitRectangle(previewPos, media.videoCodecContext->width, media.videoCodecContext->height);
    
    pcsPreview.model = (mat4){
        previewRect.width,0,0,0,
        0,previewRect.height,0,0,
        0,0,1,0,
        previewRect.x,previewRect.y,0,1,
    };

    if(!handleDragNDrop(effectsTab)) return false;

    // --------------------------- EFFECTS RACK --------------------------------
    Rect effectRackSplitter = (Rect){
        .width = SPLITTER_THICKNESS,
        .height = effectsTab.height,
        .x = effectsTab.x,
        .y = effectsTab.y
    };

    static bool usingEffectsSplitter = false;
    drawHorizontalSplitter(effectRackSplitter, &usingEffectsSplitter, &effectTabSplitterOffset);

    Rect effectRack = (Rect){
        .width = effectsTab.width - SPLITTER_THICKNESS,
        .height = effectsTab.height,
        .x = effectsTab.x + SPLITTER_THICKNESS,
        .y = effectsTab.y
    };
    drawEffectsRack(deltaTime, &currentModuleInstances, effectRack);

    // --------------------------- Timeline --------------------------------

    Rect timelineSplitter = (Rect){
        .width = timelineRect.width,
        .height = SPLITTER_THICKNESS,
        .x = timelineRect.x,
        .y = timelineRect.y
    };

    static bool usingTimelineSplitter = false;
    drawVerticalSplitter(timelineSplitter, &usingTimelineSplitter, &timelineSplitterOffset);

    Rect timelineContainer = (Rect){
        .width = timelineRect.width,
        .height = timelineRect.height - SPLITTER_THICKNESS,
        .x = timelineRect.x,
        .y = timelineRect.y + SPLITTER_THICKNESS
    };

    drawTimeline(timelineContainer, &Time, &media);

    // -------------------------------- top bar ---------------------------------------
    drawTopBar(topBarRect);

    ui_end();

    return true;
}


size_t currentAttachment = 0;
bool draw(){
    static VkImageMemoryBarrier barrier;
    currentAttachment = 0;

    //previewImage pass / vfx pass
    barrier = (VkImageMemoryBarrier){0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = previewImage1;
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

    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = previewView1,
        .renderArea = (VkExtent2D){.width = media.videoCodecContext->width, .height = media.videoCodecContext->height},
        .clearColor = (Color){0,0,0,1},
    });

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = media.videoCodecContext->width,
        .height = media.videoCodecContext->height
    });
        
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = (VkExtent2D){.width = media.videoCodecContext->width, .height = media.videoCodecContext->height},
    });

    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineImagePreview);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineImageLayoutPreview,0,1,&previewDescriptorSet,0,NULL);

    vkCmdDraw(cmd, 6, 1, 0, 0);
    vkCmdEndRendering(cmd);

    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );

    for(size_t i = 0; i < currentModuleInstances.count; i++){
        barrier.image = currentAttachment == 0 ? previewImage2 : previewImage1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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

        VfxModule* module = currentModuleInstances.items[i].module;

        vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
            .colorAttachment = currentAttachment == 0 ? previewView2 : previewView1,
            .renderArea = (VkExtent2D){.width = media.videoCodecContext->width, .height = media.videoCodecContext->height},
            .clearColor = (Color){0,0,0,1},
        });

        vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
            .width = media.videoCodecContext->width,
            .height = media.videoCodecContext->height
        });
            
        vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
            .extent = (VkExtent2D){.width = media.videoCodecContext->width, .height = media.videoCodecContext->height},
        });

        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, module->pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,module->pipelineLayout,0,1,currentAttachment == 0 ? &outDescriptorSet1 : &outDescriptorSet2,0,NULL);

        if(module->inputs.count > 0){
            vkCmdPushConstants(cmd, module->pipelineLayout, VK_SHADER_STAGE_ALL, 0, module->pushContantsSize, currentModuleInstances.items[i].inputPushConstants);
        }

        vkCmdDraw(cmd, 6, 1, 0, 0);
        vkCmdEndRendering(cmd);

        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, NULL,
            0, NULL,
            1, &barrier
        );

        currentAttachment = 1 - currentAttachment;
    }

    VkImageView swapchainImage = getSwapchainImageView();

    //preview pass
    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = swapchainImage,
        .clearColor = (Color){0,0,0, 1},
    });

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = swapchainExtent.width,
        .height = swapchainExtent.height
    });
        
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = swapchainExtent,
    });

    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinePreview);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayoutPreview,0,1,currentAttachment == 0 ? &outDescriptorSet1 : &outDescriptorSet2,0,NULL);
    vkCmdPushConstants(cmd,pipelineLayoutPreview,VK_SHADER_STAGE_ALL,0,sizeof(PushConstantsPreview), &pcsPreview);

    vkCmdDraw(cmd, 6, 1, 0, 0);

    vkCmdEndRendering(cmd);

    //sprite pass
    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = swapchainImage,
    });
    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = swapchainExtent.width,
        .height = swapchainExtent.height
    });
        
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&bindlessDescriptorSet,0,NULL);

    vkCmdPushConstants(cmd,pipelineLayout,VK_SHADER_STAGE_ALL,0,sizeof(PushConstants), &pcs);

    renderSprites(swapchainExtent.width, swapchainExtent.height);

    vkCmdEndRendering(cmd);
    return true;
}

bool postDraw(){
    return true;

    /*

    if(!rendering) return true;
    if(!didSmth) return true;

    if(stopRendering){
        rendering = false;
        stopRendering = false;
        ffmpegMediaRenderFinish(&renderContext);
        free(frameDataProcessed);
        printf("Finished rendering!\n");
        return true;
    }

    VkCommandBuffer tempCmd = beginSingleTimeCommands();
    // Add memory barrier to ensure the image is ready to be read
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = currentAttachment == 0 ? previewImage1 : previewImage2;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        tempCmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );

    endSingleTimeCommands(tempCmd);

    // Flush the command buffer to ensure the barrier executes
    vkQueueWaitIdle(graphicsQueue);

    void* frameData = currentAttachment == 0 ? previewMemoryMapped1 : previewMemoryMapped2;

    // Copy row by row, accounting for possible row pitch differences
    for(size_t i = 0; i < media.videoCodecContext->height; i++) {
        memcpy(
            (uint8_t*)frameDataProcessed + i * media.videoCodecContext->width * sizeof(uint32_t),
            (uint8_t*)frameData + i * videoVulkanStride,
            media.videoCodecContext->width * sizeof(uint32_t)
        );
    }

    Frame renderFrame = frame;
    renderFrame.video.data = frameDataProcessed;
    
    if(!ffmpegMediaRenderPassFrame(&renderContext, &renderFrame)) {
        printf("Failed to pass frame to renderer\n");
        return false;
    }

    tempCmd = beginSingleTimeCommands();
    // Restore image layout for next frame
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    vkCmdPipelineBarrier(
        tempCmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );

    endSingleTimeCommands(tempCmd);

    return true;

    */
}