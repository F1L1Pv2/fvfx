#ifndef FVFX_AUDIO_WORKER
#define FVFX_AUDIO_WORKER

#include <stdbool.h>
#include "ffmpeg_audio.h"

bool initAudioWorker(Audio* audio);
bool audio_seek(Audio* audio, double time_seconds);

#endif