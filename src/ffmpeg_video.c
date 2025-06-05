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

#include <stdint.h>

static AVFormatContext* formatContext;
static int videoStreamIndex = -1;
static AVCodecParameters* codecParameters;
static const AVCodec* codec;
static AVCodecContext* codecContext;
static AVFrame* frame;
static AVPacket* packet;
static struct SwsContext* swsContext;
static char* data;
static void* mapped;
static int vulkanImageRowPitch;

bool ffmpegInit(char* filename, VkImage* imageOut, VkDeviceMemory* imageDeviceMemoryOut, VkImageView* imageViewOut, size_t* widthOut, size_t* heightOut){
    // av_log_set_level(AV_LOG_DEBUG);

    formatContext = avformat_alloc_context();
    if(!formatContext){
        printf("Couldn't allocate formatContext\n");
        return false;
    }

    if(avformat_open_input(&formatContext,filename, NULL, NULL) < 0){
        printf("Couldn't open filename %s\n", filename);
        return false;
    }

    for (int i = 0; i < formatContext->nb_streams; i++){
        AVStream* stream = formatContext->streams[i];
        codecParameters = stream->codecpar;
        codec = avcodec_find_decoder(codecParameters->codec_id);
        if(!codec){
            continue;
        }

        if(codecParameters->codec_type == AVMEDIA_TYPE_VIDEO){
            videoStreamIndex = i;
            break;
        }
    }

    if(videoStreamIndex == -1){
        printf("Couldn't find video stream\n");
        return false;
    }

    codecContext = avcodec_alloc_context3(codec);
    if(!codecContext){
        printf("Couldn't allocate codecContext\n");
        return false;
    }

    if(avcodec_parameters_to_context(codecContext,codecParameters) < 0){
        printf("Couldn't transfer parameters to codec context\n");
        return false;
    }

    if(avcodec_open2(codecContext,codec, NULL) < 0){
        printf("Couldn't open codec\n");
        return false;
    }

    frame = av_frame_alloc();
    if(!frame){
        printf("Couldn't allocate frame\n");
        return false;
    }
    packet = av_packet_alloc();
    if(!packet){
        printf("Couldn't allocate packet\n");
        return false;
    }

    int response;
    while (av_read_frame(formatContext, packet) >= 0){
        if(packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        response = avcodec_send_packet(codecContext, packet);
        if(response < 0){
            printf("(1) Failed to decode packet: %s\n", av_err2str(response));
            return false;
        }

        response = avcodec_receive_frame(codecContext,frame);
        if(response == AVERROR(EAGAIN) || response == AVERROR_EOF){
            av_packet_unref(packet);
            continue;
        } else if (response < 0){
            printf("(2) Failed to decode packet: %s\n", av_err2str(response));
            return false;
        }

        av_packet_unref(packet);
        break;
    }

    swsContext = sws_getContext(frame->width,frame->height,codecContext->pix_fmt,
                                            frame->width,frame->height,AV_PIX_FMT_RGB0,
                                            SWS_FAST_BILINEAR, NULL, NULL, NULL);
    
    if(!swsContext){
        printf("Couldn't get swsContext\n");
        return false;
    }

    *widthOut = frame->width;
    *heightOut = frame->height;

    data = malloc(frame->width * frame->height * sizeof(uint32_t));

    uint8_t* dest[4] = {(uint8_t*)data, NULL, NULL, NULL};
    int dest_linesize[4] = {frame->width * sizeof(uint32_t), 0,0,0};
    sws_scale(swsContext,(const uint8_t* const*)&frame->data,frame->linesize,0,frame->height, dest, dest_linesize);

    if(!createImage(frame->width,frame->height,VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, imageOut, imageDeviceMemoryOut)){
        return false;
    }
    
    if(!sendDataToImage(*imageOut,data,frame->width,frame->width*sizeof(uint32_t),frame->height)){
        return false;
    }
    
    if(!createImageView(*imageOut, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, imageViewOut)){
        return false;
    }

    VkResult result = vkMapMemory(device,*imageDeviceMemoryOut,0,frame->width*frame->height*sizeof(uint32_t),0, &mapped);
    if(result != VK_SUCCESS){
        printf("Couldn't map image");
        return false;
    }

    VkImageSubresource subResource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout subResourceLayout;
    vkGetImageSubresourceLayout(device, *imageOut, &subResource, &subResourceLayout);
    vulkanImageRowPitch = subResourceLayout.rowPitch;

    return true;
}

bool ffmpegProcessFrame(){
    int response;
    while (av_read_frame(formatContext, packet) >= 0){
        if(packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        response = avcodec_send_packet(codecContext, packet);
        if(response < 0){
            printf("(1) Failed to decode packet: %s\n", av_err2str(response));
            return false;
        }

        response = avcodec_receive_frame(codecContext,frame);
        if(response == AVERROR(EAGAIN) || response == AVERROR_EOF){
            av_packet_unref(packet);
            continue;
        } else if (response < 0){
            printf("(2) Failed to decode packet: %s\n", av_err2str(response));
            return false;
        }

        av_packet_unref(packet);
        break;
    }

    uint8_t* dest[4] = {(uint8_t*)data, NULL, NULL, NULL};
    int dest_linesize[4] = {frame->width * sizeof(uint32_t), 0,0,0};
    sws_scale(swsContext,(const uint8_t* const*)&frame->data,frame->linesize,0,frame->height, dest, dest_linesize);

    int cpuPitch = frame->width*sizeof(uint32_t);

    for(int i = 0; i < frame->height; i++){
        memcpy(
            mapped + i *vulkanImageRowPitch,
            data + i * cpuPitch,
            cpuPitch
        );
    }
    
    return true;
}

void ffmpegUninit(){
    free(data);
    vkUnmapMemory(device, mapped);

    avformat_close_input(&formatContext);
    avcodec_free_context(&codecContext);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avformat_free_context(formatContext);
}