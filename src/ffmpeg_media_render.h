#ifndef FVFX_FFMPEG_MEDIA_RENDER
#define FVFX_FFMPEG_MEDIA_RENDER

#include <stdint.h>
#include <stdbool.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>

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
    AVAudioFifo* audioFifo;

    size_t videoFrameCount;
    size_t audioFrameCount;
} MediaRenderContext;

typedef enum {
    RENDER_FRAME_TYPE_NONE = 0,
    RENDER_FRAME_TYPE_VIDEO,
    RENDER_FRAME_TYPE_AUDIO
} RenderFrameType;

typedef struct {
    RenderFrameType type;
    void* data;
    size_t size; // in case of audio it means number of samples
} RenderFrame;

bool ffmpegMediaRenderInit(const char* filename, size_t width, size_t height, double fps, size_t sampleRate, bool stereo, bool hasAudio, MediaRenderContext* render);
bool ffmpegMediaRenderPassFrame(MediaRenderContext* render, const RenderFrame* frame);
void ffmpegMediaRenderFinish(MediaRenderContext* render);

#endif