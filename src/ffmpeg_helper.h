#ifndef FVFX_FFMPEG_HELPER
#define FVFX_FFMPEG_HELPER

#include <stddef.h>
#include <stdint.h>

void mix_audio(uint8_t** base, uint8_t** added, size_t nb_samples);

#endif