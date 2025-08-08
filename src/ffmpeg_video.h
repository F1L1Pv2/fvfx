#ifndef FVFX_FFMPEG_VIDEO
#define FVFX_FFMPEG_VIDEO

#include <stdbool.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

typedef enum {
    FRAME_TYPE_NONE = 0,
    FRAME_TYPE_VIDEO,
    FRAME_TYPE_AUDIO
} FrameType;

typedef struct {
    void *data;
    size_t width;
    size_t height;
} VideoFrame;

typedef struct {
    uint8_t* data;
    int size;
    int channels;
    int sampleRate;
} AudioFrame;

typedef struct{
    FrameType type;
    double frameTime;
    VideoFrame video;
    AudioFrame audio;
} Frame;

typedef struct {
    AVFormatContext* formatContext;
    int videoStreamIndex;
    AVCodecContext* codecContext;
    AVFrame* frame;
    AVPacket* packet;
    struct SwsContext* swsContext;
    double duration;
    double frameRate;

    int audioStreamIndex;
    AVCodecContext* audioCodecContext;
    AVFrame* audioFrame;
} Video;

bool ffmpegVideoInit(const char* filename, Video* video);
void ffmpegVideoUninit(Video* video);
bool ffmpegVideoGetFrame(Video* video, Frame* frame);
bool ffmpegVideoSeek(Video* video, Frame* frame, double time_seconds);

#endif