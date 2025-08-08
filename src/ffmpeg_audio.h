#ifndef FVFX_FFMPEG_AUDIO
#define FVFX_FFMPEG_AUDIO

#include <stdbool.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>

typedef struct {
    AVFormatContext* formatContext;
    int audioStreamIndex;
    AVCodecContext* codecContext;
    AVFrame* frame;
    AVPacket* packet;
    struct SwrContext* swrContext;
    double duration;

    uint32_t outChannels;
    uint32_t outSampleRate;
} Audio;

typedef struct {
    void* data;
    size_t numberSamples;
} FFmpegAudioFrame;

bool ffmpegAudioInit(const char* filename, Audio* audio, uint32_t outChannels, uint32_t outSampleRate);
void ffmpegAudioUninit(Audio* audio);
bool ffmpegAudioSeek(Audio* audio, double time_seconds);
bool ffmpegAudioGetFrame(Audio* audio, FFmpegAudioFrame* out, bool resample);
#endif