#ifndef FVFX_FFMPEG_HELPER
#define FVFX_FFMPEG_HELPER

#include <stddef.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>

void mix_audio(uint8_t** base, uint8_t** added,size_t nb_samples, size_t num_channels,enum AVSampleFormat sample_fmt,double volume, double panning);
int av_audio_fifo_add_silence(AVAudioFifo *af, enum AVSampleFormat sample_fmt, const AVChannelLayout *ch_layout, int nb_samples);

#endif