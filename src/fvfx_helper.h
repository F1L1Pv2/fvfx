#ifndef FVFX_FVFX_HELPER
#define FVFX_FVFX_HELPER

#include <libavutil/audio_fifo.h>
#include <libavutil/samplefmt.h>
#include "myProject.h"
#include "project.h"

void mix_all_layers(
    uint8_t** composedAudioBuf,   // [out] output buffer (already cleared)
    uint8_t** tempAudioBuf,       // [in] temp buffer
    MyLayer* myLayers,            // linked list of layers
    int out_audio_frame_size,     // number of frames to produce this iteration
    enum AVSampleFormat out_audio_format, // output sample format
    Project* project              // holds global mix settings (e.g., stereo flag)
);
#endif