#ifndef FVFX_FFMPEG_MEDIA_RENDER
#define FVFX_FFMPEG_MEDIA_RENDER

#include <stdint.h>
#include <stdbool.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#include "ffmpeg_media.h"

typedef struct {
    AVFormatContext* formatContext;
    AVPacket* packet;

    AVStream* videoStream;
    AVCodecContext* videoCodecContext;
    AVFrame* videoFrame;
    struct SwsContext* swsContext;
    
    int audioStreamIndex;
    AVCodecContext* audioCodecContext;
    AVStream* audioStream;
    AVFrame* audioFrame;
    AVPacket* audioPacket;
} MediaRenderContext;

bool ffmpegMediaRenderInit(const Media* sourceVideo, const char* filename, MediaRenderContext* render);
bool ffmpegMediaRenderPassFrame(MediaRenderContext* render, const Frame* frame);
void ffmpegMediaRenderFinish(MediaRenderContext* render);

#endif