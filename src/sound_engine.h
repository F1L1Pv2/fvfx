#ifndef FVFX_SOUND_ENGINE

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void* data;
    size_t numberSamples;
} SoundAudioFrame;

bool initSoundEngine();
uint32_t soundEngineGetSampleRate();
uint32_t soundEngineGetChannels();
bool soundEngineCanEnqueueFrame();
bool soundEngineEnqueueFrame(SoundAudioFrame* audioFrame);
void soundEngineResetQueue();
bool soundEngineInitialized();
void soundEngineClear();

#endif