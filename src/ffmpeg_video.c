#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "ffmpeg_video.h"
#include "engine/vulkan_globals.h"
#include "engine/vulkan_helpers.h"
#include "engine/vulkan_images.h"

// Internal helper functions
static bool readFrame(Video* video);
static bool readAndCacheFrame(Video* video);
static bool initializeVideoContext(Video* video, const char* filename);
static bool initializeDecoder(Video* video);
static bool getFirstFrame(Video* video);
static bool createConversionContext(Video* video);
static bool setupVulkanResources(Video* video, VkImage* imageOut, 
                                VkDeviceMemory* imageDeviceMemoryOut, 
                                VkImageView* imageViewOut);
static void precacheFrames(Video* video, int count);
static double getFrameTimeRaw(const Video* video);
static bool seekToTimeRaw(Video* video, double frameTime);

bool ffmpegInit(const char* filename, Video* video, 
                VkImage* imageOut, VkDeviceMemory* imageDeviceMemoryOut, 
                VkImageView* imageViewOut, size_t* widthOut, size_t* heightOut) 
{
    memset(video, 0, sizeof(Video));
    
    if (!initializeVideoContext(video, filename)) goto error;
    if (!initializeDecoder(video)) goto error;
    if (!getFirstFrame(video)) goto error;
    if (!createConversionContext(video)) goto error;
    
    *widthOut = video->frame->width;
    *heightOut = video->frame->height;
    
    // Initialize frame caching
    double frameRate = av_q2d(video->formatContext->streams[video->videoStreamIndex]->avg_frame_rate);
    if (frameRate <= 0.0) {
        frameRate = av_q2d(video->formatContext->streams[video->videoStreamIndex]->r_frame_rate);
    }
    
    const int cacheSize = (int)ceil(frameRate * 1.5);
    video->cachedFrames = initCircularBuffer(
        video->frame->width * video->frame->height * sizeof(uint32_t), 
        cacheSize
    );
    video->cachedFrameInfos = initCircularBuffer(
        sizeof(CachedFrameMetadata), 
        cacheSize
    );
    
    // Allocate frame buffer
    video->data = malloc(video->cachedFrames.item_size);
    if (!video->data) goto error;
    
    // Setup Vulkan resources
    if (!setupVulkanResources(video, imageOut, imageDeviceMemoryOut, imageViewOut)) goto error;
    
    // Cache first frame
    if (!readAndCacheFrame(video)) goto error;
    video->readData = video->cachedFrames.items;
    
    // Precache additional frames
    precacheFrames(video, 40);
    
    return true;

error:
    ffmpegUninit(video);
    return false;
}

bool ffmpegProcessFrame(Video* video, double time) {
    CachedFrameMetadata* currentMeta = (CachedFrameMetadata*)(
        video->cachedFrameInfos.items + 
        video->cachedFrameInfos.item_size * video->cachedFrameInfos.read_cur
    );
    
    if (canWriteCircularBuffer(&video->cachedFrames)) {
        if (!readAndCacheFrame(video)) return true;
    }

    if (time > currentMeta->frameTime) {
        video->readData = readCircularBuffer(&video->cachedFrames);
        readCircularBuffer(&video->cachedFrameInfos);
    }

    ffmpegRender(video);
    return true;
}

bool ffmpegSeekRaw(Video* video, double frameTime){
    int64_t target_pts = frameTime / av_q2d(video->formatContext->streams[video->videoStreamIndex]->time_base);

    int ret = av_seek_frame(video->formatContext, video->videoStreamIndex, target_pts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        printf("Seek failed: %s\n", av_err2str(ret));
        return false;
    }

    avcodec_flush_buffers(video->codecContext);

    resetCircularBuffer(&video->cachedFrames);
    resetCircularBuffer(&video->cachedFrameInfos);

    return true;
}

bool ffmpegSeek(Video* video, double time_seconds) {
    if (!ffmpegSeekRaw(video, time_seconds)) {
        return false;
    }

    do {
        if (!readFrame(video)) break;
    } while (getFrameTimeRaw(video) < time_seconds);

    if (getFrameTimeRaw(video) >= time_seconds) {
        resetCircularBuffer(&video->cachedFrames);
        resetCircularBuffer(&video->cachedFrameInfos);
        if (!writeCircularBuffer(&video->cachedFrames, video->data)) return false;
        if (!writeCircularBuffer(&video->cachedFrameInfos, &(CachedFrameMetadata){.frameTime = getFrameTimeRaw(video)})) return false;
        return true;
    }

    return false;
}

void ffmpegRender(Video* video) {
    const int cpuPitch = video->frame->width * sizeof(uint32_t);
    const int height = video->frame->height;
    
    for (int i = 0; i < height; i++) {
        memcpy(
            (char*)video->mapped + i * video->vulkanImageRowPitch,
            video->readData + i * cpuPitch,
            cpuPitch
        );
    }
}

void ffmpegUninit(Video* video) {
    if (video->data) free(video->data);
    if (video->mapped) vkUnmapMemory(device, video->mapped);
    
    if (video->swsContext) sws_freeContext(video->swsContext);
    if (video->codecContext) avcodec_free_context(&video->codecContext);
    if (video->frame) av_frame_free(&video->frame);
    if (video->packet) av_packet_free(&video->packet);
    if (video->formatContext) {
        avformat_close_input(&video->formatContext);
        avformat_free_context(video->formatContext);
    }
    
    freeCircularBuffer(video->cachedFrames);
    freeCircularBuffer(video->cachedFrameInfos);
    
    memset(video, 0, sizeof(Video));
}

double getDuration(Video* video) {
    return (double)video->formatContext->streams[video->videoStreamIndex]->duration * 
           av_q2d(video->formatContext->streams[video->videoStreamIndex]->time_base);
}

double getFrameTime(Video* video) {
    if (video->cachedFrameInfos.count == 0) return 0.0;
    CachedFrameMetadata* meta = (CachedFrameMetadata*)(
        video->cachedFrameInfos.items + 
        video->cachedFrameInfos.item_size * video->cachedFrameInfos.read_cur
    );
    return meta->frameTime;
}

static bool readFrame(Video* video){
    int response;
    bool frameDecoded = false;
    
    while (av_read_frame(video->formatContext, video->packet) >= 0) {
        if (video->packet->stream_index != video->videoStreamIndex) {
            av_packet_unref(video->packet);
            continue;
        }

        response = avcodec_send_packet(video->codecContext, video->packet);
        if (response < 0) {
            av_packet_unref(video->packet);
            continue;
        }

        response = avcodec_receive_frame(video->codecContext, video->frame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            av_packet_unref(video->packet);
            continue;
        } 
        else if (response < 0) {
            av_packet_unref(video->packet);
            return false;
        }

        av_packet_unref(video->packet);
        frameDecoded = true;
        break;
    }
    
    if (!frameDecoded) return false;
    
    // Convert frame to RGB
    uint8_t* dest[4] = {(uint8_t*)video->data, NULL, NULL, NULL};
    int dest_linesize[4] = {video->frame->width * sizeof(uint32_t), 0, 0, 0};
    sws_scale(video->swsContext, 
             (const uint8_t* const*)video->frame->data, 
             video->frame->linesize, 
             0, 
             video->frame->height, 
             dest, 
             dest_linesize);

    return true;
}

static bool readAndCacheFrame(Video* video) {
    if(!readFrame(video)) return false;
    
    // Cache frame and metadata
    if (!writeCircularBuffer(&video->cachedFrames, video->data)) return false;
    
    CachedFrameMetadata meta = {.frameTime = getFrameTimeRaw(video)};
    if (!writeCircularBuffer(&video->cachedFrameInfos, &meta)) return false;
    
    return true;
}

static bool initializeVideoContext(Video* video, const char* filename) {
    video->formatContext = avformat_alloc_context();
    if (!video->formatContext) return false;
    
    if (avformat_open_input(&video->formatContext, filename, NULL, NULL) < 0) {
        avformat_free_context(video->formatContext);
        return false;
    }
    
    if (avformat_find_stream_info(video->formatContext, NULL) < 0) {
        return false;
    }
    
    return true;
}

static bool initializeDecoder(Video* video) {
    // Find video stream
    video->videoStreamIndex = -1;
    for (int i = 0; i < video->formatContext->nb_streams; i++) {
        AVStream* stream = video->formatContext->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video->videoStreamIndex = i;
            video->codecParameters = stream->codecpar;
            break;
        }
    }
    
    if (video->videoStreamIndex == -1) return false;
    
    // Find decoder
    video->codec = avcodec_find_decoder(video->codecParameters->codec_id);
    if (!video->codec) return false;
    
    // Allocate codec context
    video->codecContext = avcodec_alloc_context3(video->codec);
    if (!video->codecContext) return false;
    
    if (avcodec_parameters_to_context(video->codecContext, video->codecParameters) < 0) {
        return false;
    }
    
    if (avcodec_open2(video->codecContext, video->codec, NULL) < 0) {
        return false;
    }
    
    // Allocate frame and packet
    video->frame = av_frame_alloc();
    video->packet = av_packet_alloc();
    return video->frame && video->packet;
}

static bool getFirstFrame(Video* video) {
    while (av_read_frame(video->formatContext, video->packet) >= 0) {
        if (video->packet->stream_index != video->videoStreamIndex) {
            av_packet_unref(video->packet);
            continue;
        }
        
        int response = avcodec_send_packet(video->codecContext, video->packet);
        av_packet_unref(video->packet);
        
        if (response < 0) continue;
        
        response = avcodec_receive_frame(video->codecContext, video->frame);
        if (response == 0) return true;
        if (response == AVERROR_EOF) break;
    }
    return false;
}

static bool createConversionContext(Video* video) {
    video->swsContext = sws_getContext(
        video->frame->width, video->frame->height, video->codecContext->pix_fmt,
        video->frame->width, video->frame->height, AV_PIX_FMT_RGBA,
        SWS_FAST_BILINEAR, NULL, NULL, NULL
    );
    return video->swsContext != NULL;
}

static bool setupVulkanResources(Video* video, VkImage* imageOut, 
                               VkDeviceMemory* imageDeviceMemoryOut, 
                               VkImageView* imageViewOut) 
{
    // Create image
    if (!createImage(video->frame->width, video->frame->height,
                   VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   imageOut, imageDeviceMemoryOut)) {
        return false;
    }

    VkCommandBuffer tempCmd = beginSingleTimeCommands();
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = *imageOut;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        tempCmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );
    endSingleTimeCommands(tempCmd);
    
    // Create image view
    if (!createImageView(*imageOut, VK_FORMAT_R8G8B8A8_UNORM, 
                        VK_IMAGE_ASPECT_COLOR_BIT, imageViewOut)) {
        return false;
    }
    
    // Map memory and get layout info
    VkImageSubresource subresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .arrayLayer = 0
    };
    
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(device, *imageOut, &subresource, &layout);
    video->vulkanImageRowPitch = layout.rowPitch;
    
    VkResult result = vkMapMemory(device, *imageDeviceMemoryOut, 0, 
                                 layout.size, 0, &video->mapped);
    return result == VK_SUCCESS;
}

static void precacheFrames(Video* video, int count) {
    for (int i = 0; i < count && canWriteCircularBuffer(&video->cachedFrames); i++) {
        readAndCacheFrame(video);
    }
}

static double getFrameTimeRaw(const Video* video) {
    return (double)video->frame->pts * 
           av_q2d(video->formatContext->streams[video->videoStreamIndex]->time_base);
}