#include "3rdparty/miniaudio.h"
#include "sound_engine.h"
#include <string.h>
#include <malloc.h>
#include <stdatomic.h>
#include <stdio.h>

typedef struct {
    SoundAudioFrame* items;
    size_t count;
    atomic_int read_cur;
    atomic_int write_cur;
} AudioFrameFIFO;

static void createAudioFrameFIFO(size_t count, AudioFrameFIFO* audioFrameFIFO) {
    audioFrameFIFO->items = calloc(sizeof(SoundAudioFrame) * count,1);
    audioFrameFIFO->count = count;
    audioFrameFIFO->read_cur = 0;
    audioFrameFIFO->write_cur = 0;
}

static SoundAudioFrame* readAudioFrameFIFO(AudioFrameFIFO* audioFrameFIFO) {
    if (audioFrameFIFO->read_cur == audioFrameFIFO->write_cur) return NULL;
    SoundAudioFrame* data = &audioFrameFIFO->items[audioFrameFIFO->read_cur];
    audioFrameFIFO->read_cur = (audioFrameFIFO->read_cur + 1) % audioFrameFIFO->count;
    return data;
}

static bool canWriteAudioFrameFIFO(AudioFrameFIFO* audioFrameFIFO) {
    return !((audioFrameFIFO->write_cur + 1) % audioFrameFIFO->count == audioFrameFIFO->read_cur);
}

static bool writeAudioFrameFIFO(AudioFrameFIFO* audioFrameFIFO, SoundAudioFrame* item) {
    if (!canWriteAudioFrameFIFO(audioFrameFIFO)) return false;
    if(audioFrameFIFO->items[audioFrameFIFO->write_cur].data){
        free(audioFrameFIFO->items[audioFrameFIFO->write_cur].data);
        audioFrameFIFO->items[audioFrameFIFO->write_cur].data = NULL;
    }
    memcpy(&audioFrameFIFO->items[audioFrameFIFO->write_cur], item, sizeof(SoundAudioFrame));
    audioFrameFIFO->write_cur = (audioFrameFIFO->write_cur + 1) % audioFrameFIFO->count;
    return true;
}

static atomic_bool clearing = false;

static void clearAudioFrameFIFO(AudioFrameFIFO* fifo) {
    clearing = true;
    while (true) {
        SoundAudioFrame* frame = readAudioFrameFIFO(fifo);
        if (!frame) break;
        if (frame->data) {
            free(frame->data);
            frame->data = NULL;
        }
        memset(frame, 0, sizeof(*frame));
    }
    fifo->read_cur = 0;
    fifo->write_cur = 0;
    clearing = false;
}

AudioFrameFIFO audioFrameFifo = {0};

bool soundEngineCanEnqueueFrame(){
    return canWriteAudioFrameFIFO(&audioFrameFifo);
}

bool soundEngineEnqueueFrame(SoundAudioFrame* audioFrame){
    return writeAudioFrameFIFO(&audioFrameFifo, audioFrame);
}

void soundEngineClear(){
    clearAudioFrameFIFO(&audioFrameFifo);
}

static SoundAudioFrame* currentFrame = NULL;
static size_t currentPos = 0;

void soundEngineResetQueue() {
    currentFrame = NULL;
    currentPos = 0;
    audioFrameFifo.read_cur = 0;
    audioFrameFifo.write_cur = 0;
}

extern atomic_bool playing;
extern double Time;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    float* output = (float*)pOutput;
    const ma_uint32 channels = pDevice->playback.channels;
    const size_t totalSamplesNeeded = frameCount * channels;
    size_t samplesCopied = 0;

    if(!playing || clearing) {
        memset(output, 0, totalSamplesNeeded);
        return;
    }
    if(playing && !clearing) Time += (double)frameCount / (double)pDevice->sampleRate;

    while (samplesCopied < totalSamplesNeeded) {
        if (!currentFrame) {
            currentFrame = readAudioFrameFIFO(&audioFrameFifo);
            currentPos = 0;
            if (!currentFrame) break;
        }

        if(currentFrame && currentFrame->data){
            const size_t samplesAvailable = currentFrame->numberSamples * channels - currentPos;
            const size_t samplesNeeded = totalSamplesNeeded - samplesCopied;
            const size_t samplesToCopy = (samplesAvailable < samplesNeeded)
                ? samplesAvailable
                : samplesNeeded;

            float* frameData = (float*)currentFrame->data;
            memcpy(output + samplesCopied,
                frameData + currentPos,
                samplesToCopy * sizeof(float));
    
            samplesCopied += samplesToCopy;
            currentPos += samplesToCopy;
            if (currentPos >= currentFrame->numberSamples * channels) currentFrame = NULL;
        }
    }

    if (samplesCopied < totalSamplesNeeded) {
        memset(output + samplesCopied, 0, (totalSamplesNeeded - samplesCopied) * sizeof(float));
    }
}

ma_device soundDevice;

static bool inited = false;

bool initSoundEngine() {
    createAudioFrameFIFO(200,&audioFrameFifo);
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 0;
    config.dataCallback = data_callback;
    config.pUserData = NULL;

    ma_result result = ma_device_init(NULL, &config, &soundDevice);
    if (result != MA_SUCCESS)
    {
        printf("Failed to initialize audio device. Error: %s\n", ma_result_description(result));
        return false;
    }

    size_t nameSize = 0;
    result = ma_device_get_name(&soundDevice, ma_device_type_playback, NULL, 0, &nameSize);
    if (nameSize != 0 && result == MA_SUCCESS) {
        char* name = malloc(nameSize + 1);
        if (ma_device_get_name(&soundDevice, ma_device_type_playback, name, nameSize + 1, NULL) == MA_SUCCESS) {
            printf("[Sound Engine] Using: %s audio device\n", name);
        }
        free(name);
    }

    ma_device_start(&soundDevice);
    inited = true;
    return true;
}

uint32_t soundEngineGetSampleRate(){
    return soundDevice.sampleRate;
}

uint32_t soundEngineGetChannels(){
    return soundDevice.playback.channels;
}

bool soundEngineInitialized(){
    return inited;
}