#ifndef FVFX_FFMPEG_VIDEO
#define FVFX_FFMPEG_VIDEO

#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include "circular_buffer.h"

typedef struct {
    double frameTime;
} CachedFrameMetadata;

typedef struct {
    AVFormatContext* formatContext;
    int videoStreamIndex;
    AVCodecParameters* codecParameters;
    const AVCodec* codec;
    AVCodecContext* codecContext;
    AVFrame* frame;
    AVPacket* packet;
    struct SwsContext* swsContext;
    char* data;
    char* readData;
    void* mapped;
    int vulkanImageRowPitch;
    
    CircularBuffer cachedFrames;
    CircularBuffer cachedFrameInfos;
} Video;

bool ffmpegInit(const char* filename, Video* videoOut, 
                VkImage* imageOut, VkDeviceMemory* imageDeviceMemoryOut, 
                VkImageView* imageViewOut, size_t* widthOut, size_t* heightOut);
bool ffmpegProcessFrame(Video* video, double time);
bool ffmpegSeek(Video* video, double time_seconds);
void ffmpegRender(Video* video);
void ffmpegUninit(Video* video);

double getDuration(Video* video);
double getFrameTime(Video* video);

#endif