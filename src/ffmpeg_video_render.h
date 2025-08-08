#ifndef FVFX_FFMPEG_RENDER
#define FVFX_FFMPEG_RENDER

#include <stdint.h>
#include <stdbool.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#include "ffmpeg_video.h"

typedef struct {
    AVFormatContext* formatContext;
    AVStream* stream;
    AVCodecContext* codecContext;
    AVFrame* frame;
    AVPacket* packet;
    struct SwsContext* swsContext;
    size_t frameCounter;
    size_t width;
    size_t height;
    double frameRate;
    
    int audioStreamIndex;
    AVCodecContext* audioCodecContext;
    AVStream* audioStream;
    AVFrame* audioFrame;
    AVPacket* audioPacket;
} VideoRenderContext;

bool ffmpegVideoRenderInit(const Video* sourceVideo, const char* filename, VideoRenderContext* render);
bool ffmpegVideoRenderPassFrame(VideoRenderContext* render, const Frame* frame);
void ffmpegVideoRenderFinish(VideoRenderContext* render);

#endif