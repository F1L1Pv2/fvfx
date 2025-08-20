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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "ffmpeg_media.h"
#include "ffmpeg_media_render.h"

#define NOB_STRIP_PREFIX
#include "nob.h"

typedef struct{
    double offset;
    double duration;
} Slice;

typedef struct{
    Slice* items;
    size_t count;
    size_t capacity;
} Slices;

static char* str_dup_range(const char* start, size_t len) {
    char* s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

/**
 * Parse slices text buffer.
 * - buf/size: input text buffer
 * - out_slices: dynamic array to fill
 * - out_media: pointer to char* (caller frees)
 * - out_output: pointer to char* (caller frees)
 */
int parse_slices(const char* buf, size_t size,
                 Slices* out_slices,
                 char** out_media,
                 char** out_output)
{
    *out_media = NULL;
    *out_output = NULL;

    const char* end = buf + size;
    const char* line = buf;

    while (line < end) {
        const char* next = memchr(line, '\n', (size_t)(end - line));
        size_t line_len = next ? (size_t)(next - line) : (size_t)(end - line);

        if (line_len > 0 && line[line_len - 1] == '\r') {
            line_len--;
        }

        const char* p = line;
        while (p < line + line_len && isspace((unsigned char)*p)) p++;

        if (p < line + line_len) {
            if (strncmp(p, "output:", 7) == 0) {
                p += 7;
                while (p < line + line_len && isspace((unsigned char)*p)) p++;
                *out_output = str_dup_range(p, (size_t)(line + line_len - p));
            } else if (strncmp(p, "media:", 6) == 0) {
                p += 6;
                while (p < line + line_len && isspace((unsigned char)*p)) p++;
                *out_media = str_dup_range(p, (size_t)(line + line_len - p));
            } else if (isdigit((unsigned char)*p)) {
                int h = 0, m = 0, s = 0, ms = 0;
                double duration = 0.0;

                int parsed = sscanf(p, "%d:%d:%d:%d %lf", &h, &m, &s, &ms, &duration);
                if (parsed == 5) {
                    double offset = (double)h * 3600.0 +
                                    (double)m * 60.0 +
                                    (double)s +
                                    (double)ms / 1000.0;

                    Slice slice = { offset, duration };
                    da_append(out_slices, slice);
                } else {
                    fprintf(stderr, "Warning: could not parse line: %.*s\n",
                            (int)line_len, line);
                }
            }
        }

        line = next ? next + 1 : end;
    }

    return 0;
}


void remove_carriage_return_from_str(char* data){
    char* current_pos = data;
    while ((current_pos = strchr(current_pos, '\r'))) {
        memmove(current_pos, current_pos + 1, strlen(current_pos + 1) + 1);
    }
}

bool applyShadersOnFrame(   Frame* frameIn,
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

    for(size_t i = 0; i < frame->height; i++) {
        memcpy(
            (uint8_t*)frame->data + i * frame->width * sizeof(uint32_t),
            (uint8_t*)colorData + i * colorStride,
            frame->width * sizeof(uint32_t)
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

int main(int argc, char** argv){
    if(argc < 2){
        fprintf(stderr, "Provide filename!\n");
        return 1;
    }

    if(!initialize_vulkan()) return 1;
    if(!getDevice()) return 1;
    if(!initCommandPool()) return 1;
    if(!initCommandBuffer()) return 1;
    if(!createAllNeededSyncrhonizationObjects()) return 1;
    if(!initDescriptorPool()) return 1;
    if(!initSamplers()) return 1;

    char* filename = NULL;
    char* outputFilename = "output.mp4";
    Slices slices = {0};

    String_Builder sb = {0};
    if(!read_entire_file(argv[1], &sb)){
        fprintf(stderr, "Couldn't open project file!\n");
        return 1;
    }

    remove_carriage_return_from_str(sb.items);

    if(parse_slices(sb.items, sb.count, &slices, &filename, &outputFilename) != 0) return 1;

    printf("%s:%s\n", filename, outputFilename);

    printf("Slices %zu:\n", slices.count);
    for(size_t i = 0; i < slices.count; i++){
        Slice* slice = &slices.items[i];
        printf("%lf:%lf\n", slice->offset, slice->duration);
    }
    
    printf("Hjello Freunder!\n");

    // shaders init

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

    if(!compileShader(vertexShaderSrc, shaderc_vertex_shader, &vertexShader)) return 1;

    const char* fragmentShaderSrc = 
        "#version 450\n"
        "layout(location = 0) out vec4 outColor;\n"
        "layout(location = 0) in vec2 uv;\n"

        "layout (set = 0, binding = 0) uniform sampler2D imageIN;\n"

        "void main() {"
            "outColor = texture(imageIN, uv) * vec4(uv,1,1);"
        "}";

    VkShaderModule fragmentShader;
    if(!compileShader(fragmentShaderSrc, shaderc_fragment_shader, &fragmentShader)) return 1;

    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {0};
    descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorSetLayoutBinding.descriptorCount = 1;
    descriptorSetLayoutBinding.binding = 0;
    descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {0};
    descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.bindingCount  = 1;
    descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;

    VkDescriptorSetLayout vfxDescriptorSetLayout;
    if(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &vfxDescriptorSetLayout) != VK_SUCCESS){
        printf("ERROR\n");
        return 1;
    }

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &vfxDescriptorSetLayout;

    VkDescriptorSet outDescriptorSet;
    vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, &outDescriptorSet);

    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    if(!createGraphicPipeline(
        vertexShader,fragmentShader, 
        &pipeline, 
        &pipelineLayout,
        .descriptorSetLayoutCount = 1,
        .descriptorSetLayouts = &vfxDescriptorSetLayout,
        .outColorFormat = &colorFormat
    )) return 1;

    // ffmpeg init

    Media media = {0};
    if(!ffmpegMediaInit(filename, &media)){
        fprintf(stderr, "Couldn't initialize ffmpeg media at %s!\n", filename);
        return 1;
    }

    VkImage videoInImage;
    VkDeviceMemory videoInImageMemory;
    VkImageView videoInImageView;
    size_t videoInImageStride;
    void* videoInImageMapped;

    if(!createMyImage(&videoInImage,
        media.videoCodecContext->width, 
        media.videoCodecContext->height, 
        &videoInImageMemory, &videoInImageView, 
        &videoInImageStride, 
        &videoInImageMapped,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    )) return 1;
    transitionMyImage(videoInImage, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    // --------------------- updating descriptor set -------------------------
    VkDescriptorImageInfo descriptorImageInfo = {0};
    VkWriteDescriptorSet writeDescriptorSet = {0};

    descriptorImageInfo.sampler = samplerLinear;
    descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorImageInfo.imageView = videoInImageView;

    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.dstSet = outDescriptorSet;
    writeDescriptorSet.dstBinding = 0;
    writeDescriptorSet.dstArrayElement = 0;
    writeDescriptorSet.pImageInfo = &descriptorImageInfo;

    vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);

    VkImage videoOutImage;
    VkDeviceMemory videoOutImageMemory;
    VkImageView videoOutImageView;
    size_t videoOutImageStride;
    void* videoOutImageMapped;

    if(!createMyImage(&videoOutImage,
        media.videoCodecContext->width, 
        media.videoCodecContext->height, 
        &videoOutImageMemory, &videoOutImageView, 
        &videoOutImageStride, 
        &videoOutImageMapped,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    )) return 1;
    transitionMyImage(videoOutImage, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // initializing render context

    MediaRenderContext renderContext = {0};
    if(!ffmpegMediaRenderInit(&media, outputFilename, &renderContext)){
        fprintf(stderr, "Couldn't initialize ffmpeg media renderer!\n");
        return 1;
    }

    double duration = ffmpegMediaDuration(&media);
    size_t currentSlice = 0;

    double checkDuration = slices.items[currentSlice].duration;
    if(checkDuration == -1) checkDuration = duration - slices.items[currentSlice].offset;

    double localTime = 0;
    double timeBase = 0;
    Frame frame = {0};

    ffmpegMediaSeek(&media, &frame, slices.items[currentSlice].offset);

    while(true){
        if(localTime >= checkDuration){
            currentSlice++;
            if(currentSlice >= slices.count) break;
            localTime = 0;
            timeBase+=checkDuration;
            checkDuration = slices.items[currentSlice].duration;
            if(checkDuration == -1) checkDuration = duration - slices.items[currentSlice].offset;
            ffmpegMediaSeek(&media, &frame, slices.items[currentSlice].offset);
        }

        if(!ffmpegMediaGetFrame(&media, &frame)) break;
        localTime = frame.frameTime - slices.items[currentSlice].offset;
        frame.frameTime = timeBase + localTime;

        if(frame.type == FRAME_TYPE_VIDEO){
            if(!applyShadersOnFrame(
                &frame,
                videoInImageMapped,
                videoInImageStride,

                videoOutImage, 
                videoOutImageView, 
                videoOutImageMapped, 
                videoOutImageStride, 

                pipeline, 
                pipelineLayout, 
                &outDescriptorSet)) break;
        }

        ffmpegMediaRenderPassFrame(&renderContext, &frame);
    }

    ffmpegMediaRenderFinish(&renderContext);

    printf("Finished rendering!\n");

    return 0;
}