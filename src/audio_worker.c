#include "audio_worker.h"
#include <threads.h>
#include <stdatomic.h>
#include "sound_engine.h"
#include "ffmpeg_audio.h"
#include "engine/platform.h"
#include <stdbool.h>

extern atomic_bool playing;
atomic_bool seeking = false;
extern double Time;

int audioWorker(void* data){
    Audio* audio = (Audio*)data;
    AudioFrame audioFrame = {0};

    while(true){
        while (seeking || !playing) platform_sleep(1);

        if(!ffmpegAudioGetFrame(audio, (FFmpegAudioFrame*)&audioFrame, true)) continue;

        while(!soundEngineCanEnqueueFrame()){
            platform_sleep(1);
        }
    
        soundEngineEnqueueFrame(&audioFrame);
    }

    return 0;
}

bool initAudioWorker(Audio* audio){
    thrd_t audioWorkerThread = {0};
    if(thrd_create(&audioWorkerThread,audioWorker,audio) != thrd_success) return false;
    thrd_detach(audioWorkerThread);
    return true;
}

bool audio_seek(Audio* audio, double time_seconds){
    seeking = true;
    bool result = ffmpegAudioSeek(audio, time_seconds);
    seeking = false;
    soundEngineResetQueue();
    return result;
}