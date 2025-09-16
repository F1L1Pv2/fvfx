#ifndef FVFX_FFMPEG_MEDIA
#define FVFX_FFMPEG_MEDIA

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

typedef enum {
    FRAME_TYPE_NONE = 0,
    FRAME_TYPE_VIDEO,
    FRAME_TYPE_AUDIO
} FrameType;

typedef struct {
    uint32_t *data;
    size_t width;
    size_t height;
} VideoFrame;

typedef struct {
    uint8_t** data;
    size_t nb_samples;
    int capacity;
    size_t count;
} AudioFrame;

typedef struct{
    FrameType type;
    int64_t pts;
    VideoFrame video;
    AudioFrame audio;
} Frame;

typedef struct {
    AVFormatContext* formatContext;
    AVPacket* packet;
    Frame tempFrame;

    AVStream* videoStream;
    AVCodecContext* videoCodecContext;
    AVFrame* videoFrame;
    struct SwsContext* swsContext;
    bool isImage;

    AVStream* audioStream;
    AVCodecContext* audioCodecContext;
    AVFrame* audioFrame;
    struct SwrContext* swrContext;
} Media;

bool ffmpegMediaInit(const char* filename, size_t desiredSampleRate, bool desiredStereo, enum AVSampleFormat desiredFormat, Media* media);
void ffmpegMediaUninit(Media* media);
bool ffmpegMediaGetFrame(Media* media, Frame* frame);
bool ffmpegMediaSeek(Media* media, double time_seconds);
double ffmpegMediaDuration(Media* media);

#endif