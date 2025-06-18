#ifndef FVFX_FFMPEG_VIDEO
#define FVFX_FFMPEG_VIDEO

#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

typedef struct{
    void *data;
    double frameTime;
    size_t width;
    size_t height;
} Frame;

typedef struct {
    AVFormatContext* formatContext;
    int videoStreamIndex;
    int audioStreamIndex;
    AVCodecContext* codecContextVideo;
    AVCodecContext* codecContextAudio;
    AVFrame* frame;
    AVPacket* packet;
    struct SwsContext* swsContext;
    struct SwrContext* swrContext;
    double duration;
    double frameRate;
} Video;

bool ffmpegInit(const char* filename, Video* video);
void ffmpegUninit(Video* video);
bool ffmpegGetFrame(Video* video, Frame* frame, bool skip_audio);
bool ffmpegSeek(Video* video, Frame* frame, double time_seconds);

#endif