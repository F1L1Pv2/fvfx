#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "vulkan/vulkan.h"

#include "engine/vulkan_globals.h"
#include "ffmpeg_video.h"
#include "engine/vulkan_helpers.h"
#include "engine/vulkan_images.h"

#include <malloc.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"

#include "circular_buffer.h"

#include <stdint.h>

bool getFFmpegVideo(Video* video){
    int response;
    while (av_read_frame(video->formatContext, video->packet) >= 0){
        if(video->packet->stream_index != video->videoStreamIndex) {
            av_packet_unref(video->packet);
            continue;
        }

        response = avcodec_send_packet(video->codecContext, video->packet);
        if(response < 0){
            printf("(1) Failed to decode video->packet: %s\n", av_err2str(response));
            return false;
        }

        response = avcodec_receive_frame(video->codecContext,video->frame);
        if(response == AVERROR(EAGAIN) || response == AVERROR_EOF){
            av_packet_unref(video->packet);
            continue;
        } else if (response < 0){
            printf("(2) Failed to decode video->packet: %s\n", av_err2str(response));
            return false;
        }

        av_packet_unref(video->packet);
        break;
    }

    uint8_t* dest[4] = {(uint8_t*)video->data, NULL, NULL, NULL};
    int dest_linesize[4] = {video->frame->width * sizeof(uint32_t), 0,0,0};
    sws_scale(video->swsContext,(const uint8_t* const*)&video->frame->data,video->frame->linesize,0,video->frame->height, dest, dest_linesize);

    return true;
}

double getFrameTimeRaw(Video* video);

bool ffmpegInit(char* filename, Video* video, VkImage* imageOut, VkDeviceMemory* imageDeviceMemoryOut, VkImageView* imageViewOut, size_t* widthOut, size_t* heightOut){
    // av_log_set_level(AV_LOG_DEBUG);

    video->videoStreamIndex = -1;

    video->formatContext = avformat_alloc_context();
    if(!video->formatContext){
        printf("Couldn't allocate video->formatContext\n");
        return false;
    }

    if(avformat_open_input(&video->formatContext,filename, NULL, NULL) < 0){
        printf("Couldn't open filename %s\n", filename);
        return false;
    }

    for (int i = 0; i < video->formatContext->nb_streams; i++){
        AVStream* stream = video->formatContext->streams[i];
        video->codecParameters = stream->codecpar;
        video->codec = avcodec_find_decoder(video->codecParameters->codec_id);
        if(!video->codec){
            continue;
        }

        if(video->codecParameters->codec_type == AVMEDIA_TYPE_VIDEO){
            video->videoStreamIndex = i;
            break;
        }
    }

    if(video->videoStreamIndex == -1){
        printf("Couldn't find video stream\n");
        return false;
    }

    video->codecContext = avcodec_alloc_context3(video->codec);
    if(!video->codecContext){
        printf("Couldn't allocate video->codecContext\n");
        return false;
    }

    if(avcodec_parameters_to_context(video->codecContext,video->codecParameters) < 0){
        printf("Couldn't transfer parameters to video->codec context\n");
        return false;
    }

    if(avcodec_open2(video->codecContext,video->codec, NULL) < 0){
        printf("Couldn't open video->codec\n");
        return false;
    }

    video->frame = av_frame_alloc();
    if(!video->frame){
        printf("Couldn't allocate video->frame\n");
        return false;
    }
    video->packet = av_packet_alloc();
    if(!video->packet){
        printf("Couldn't allocate video->packet\n");
        return false;
    }

    int response;
    while (av_read_frame(video->formatContext, video->packet) >= 0){
        if(video->packet->stream_index != video->videoStreamIndex) {
            av_packet_unref(video->packet);
            continue;
        }

        response = avcodec_send_packet(video->codecContext, video->packet);
        if(response < 0){
            printf("(1) Failed to decode video->packet: %s\n", av_err2str(response));
            return false;
        }

        response = avcodec_receive_frame(video->codecContext,video->frame);
        if(response == AVERROR(EAGAIN) || response == AVERROR_EOF){
            av_packet_unref(video->packet);
            continue;
        } else if (response < 0){
            printf("(2) Failed to decode video->packet: %s\n", av_err2str(response));
            return false;
        }

        av_packet_unref(video->packet);
        break;
    }

    video->swsContext = sws_getContext(video->frame->width,video->frame->height,video->codecContext->pix_fmt,
                                            video->frame->width,video->frame->height,AV_PIX_FMT_RGB0,
                                            SWS_FAST_BILINEAR, NULL, NULL, NULL);
    
    if(!video->swsContext){
        printf("Couldn't get video->swsContext\n");
        return false;
    }

    *widthOut = video->frame->width;
    *heightOut = video->frame->height;

    double frame_rate = av_q2d(video->formatContext->streams[video->videoStreamIndex]->avg_frame_rate);
    if (frame_rate <= 0.0) {
        frame_rate = av_q2d(video->formatContext->streams[video->videoStreamIndex]->r_frame_rate);
    }

    video->cachedFrames = initCircularBuffer(video->frame->width*video->frame->height*sizeof(uint32_t),floor(frame_rate*1.5));
    video->cachedFrameInfos = initCircularBuffer(sizeof(CachedFrameMetadata),floor(frame_rate*1.5));

    video->data = malloc(video->frame->width * video->frame->height * sizeof(uint32_t));

    uint8_t* dest[4] = {(uint8_t*)video->data, NULL, NULL, NULL};
    int dest_linesize[4] = {video->frame->width * sizeof(uint32_t), 0,0,0};
    sws_scale(video->swsContext,(const uint8_t* const*)&video->frame->data,video->frame->linesize,0,video->frame->height, dest, dest_linesize);

    if(!writeCircularBuffer(&video->cachedFrames,video->data)) return false;
    if(!writeCircularBuffer(&video->cachedFrameInfos, &(CachedFrameMetadata){.frameTime = getFrameTimeRaw(video)})) return false;
    video->readData = (video->cachedFrames.items + video->cachedFrames.item_size * video->cachedFrames.read_cur);

    if(!createImage(video->frame->width,video->frame->height,VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, imageOut, imageDeviceMemoryOut)){
        return false;
    }
    
    if(!sendDataToImage(*imageOut,video->data,video->frame->width,video->frame->width*sizeof(uint32_t),video->frame->height)){
        return false;
    }
    
    if(!createImageView(*imageOut, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, imageViewOut)){
        return false;
    }

    VkImageSubresource subResource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout subResourceLayout;
    vkGetImageSubresourceLayout(device, *imageOut, &subResource, &subResourceLayout);
    video->vulkanImageRowPitch = subResourceLayout.rowPitch;

    VkResult result = vkMapMemory(device,*imageDeviceMemoryOut,0,video->vulkanImageRowPitch*video->frame->height,0, &video->mapped);
    if(result != VK_SUCCESS){
        printf("Couldn't map image");
        return false;
    }

    //precache 20 frames
    for(int i = 0; i < 40 && canWriteCircularBuffer(&video->cachedFrames); i++){
        if(!getFFmpegVideo(video)) break;
        if(!writeCircularBuffer(&video->cachedFrames,video->data)) break;
        if(!writeCircularBuffer(&video->cachedFrameInfos, &(CachedFrameMetadata){.frameTime = getFrameTimeRaw(video)})) break;
    }

    return true;
}

void ffmpegRender(Video* video){
    int cpuPitch = video->frame->width*sizeof(uint32_t);

    for(int i = 0; i < video->frame->height; i++){
        memcpy(
            video->mapped + i *video->vulkanImageRowPitch,
            video->readData + i * cpuPitch,
            cpuPitch
        );
    }
}

bool ffmpegProcessFrame(Video* video, double time){
    CachedFrameMetadata cachedFrameMetadata = *(CachedFrameMetadata*)(video->cachedFrameInfos.items + video->cachedFrameInfos.item_size * video->cachedFrameInfos.read_cur);
    
    if(canWriteCircularBuffer(&video->cachedFrames)){
        if(!getFFmpegVideo(video)) return false;
        if(!writeCircularBuffer(&video->cachedFrames,video->data)) return false;
        if(!writeCircularBuffer(&video->cachedFrameInfos, &(CachedFrameMetadata){.frameTime = getFrameTimeRaw(video)})) return false;
    }

    if(time > cachedFrameMetadata.frameTime){
        video->readData = readCircularBuffer(&video->cachedFrames);
        
        if(!video->readData) {
            video->cachedFrames.read_cur = 0;
            video->cachedFrameInfos.read_cur = 0;
            return true;
        }

        readCircularBuffer(&video->cachedFrameInfos);
    }

    ffmpegRender(video);
    
    return true;
}

double getFrameTimeRaw(Video* video){
    return (double)video->frame->pts * av_q2d(video->formatContext->streams[video->videoStreamIndex]->time_base);
}
double getFrameTime(Video* video){
    return ((CachedFrameMetadata*)(video->cachedFrameInfos.items + video->cachedFrameInfos.item_size*video->cachedFrameInfos.read_cur))->frameTime;
}

double getDuration(Video* video){
    return (double)video->formatContext->streams[video->videoStreamIndex]->duration * av_q2d(video->formatContext->streams[video->videoStreamIndex]->time_base);
}

bool ffmpegSeekRaw(Video* video, double frameTime){
    int64_t target_pts = frameTime / av_q2d(video->formatContext->streams[video->videoStreamIndex]->time_base);

    int ret = av_seek_frame(video->formatContext, video->videoStreamIndex, target_pts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        printf("Seek failed: %s\n", av_err2str(ret));
        return false;
    }

    avcodec_flush_buffers(video->codecContext);

    video->cachedFrames.read_cur = 0;
    video->cachedFrames.write_cur = 0;
    video->cachedFrameInfos.read_cur = 0;
    video->cachedFrameInfos.write_cur = 0;

    for(int i = 0; i < 40 && canWriteCircularBuffer(&video->cachedFrames); i++){
        if(!getFFmpegVideo(video)) break;
        if(!writeCircularBuffer(&video->cachedFrames,video->data)) break;
        if(!writeCircularBuffer(&video->cachedFrameInfos, &(CachedFrameMetadata){.frameTime = getFrameTimeRaw(video)})) break;
    }

    return true;
}

// Helper function to get video->frame by exact time (for precise seeking)
bool ffmpegSeek(Video* video, double time_seconds) {
    if (!ffmpegSeekRaw(video, time_seconds)) {
        return false;
    }

    // Now find the exact video->frame
    do {
        if (!getFFmpegVideo(video)) break;
    } while (getFrameTimeRaw(video) < time_seconds);

    // If we found the video->frame, add it to cache
    if (getFrameTimeRaw(video) >= time_seconds) {
        video->cachedFrames.read_cur = 0;
        video->cachedFrames.write_cur = 0;
        video->cachedFrameInfos.read_cur = 0;
        video->cachedFrameInfos.write_cur = 0;
        if (!writeCircularBuffer(&video->cachedFrames, video->data)) return false;
        if (!writeCircularBuffer(&video->cachedFrameInfos, &(CachedFrameMetadata){.frameTime = getFrameTimeRaw(video)})) return false;
        return true;
    }

    return false;
}

void ffmpegUninit(Video* video){
    free(video->data);
    vkUnmapMemory(device, video->mapped);

    avformat_close_input(&video->formatContext);
    avcodec_free_context(&video->codecContext);
    av_frame_free(&video->frame);
    av_packet_free(&video->packet);
    avformat_free_context(video->formatContext);
}