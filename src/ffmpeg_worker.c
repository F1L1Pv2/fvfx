#include "ffmpeg_worker.h"

#include "ffmpeg_media.h"
#include "ffmpeg_media_render.h"

#include "sound_engine.h"
#include <threads.h>
#include <stdatomic.h>
#include <libswresample/swresample.h>

typedef enum {
    MEDIA_COMMAND_SEEK = 0,
    MEDIA_COMMAND_RESUME,
    MEDIA_COMMAND_PAUSE,
} MediaCommandType;

typedef struct{
    double frameTime;
} MediaCommandSeek;

typedef struct {
    MediaCommandType type;
    union{
        MediaCommandSeek seek;
    } as;
} MediaCommand;

typedef struct {
    MediaCommand* items;
    size_t count;
    atomic_int read_cur;
    atomic_int write_cur;
} MediaCommandFIFO;

static void createMediaCommandFIFO(size_t count, MediaCommandFIFO* mediaCommandFIFO) {
    mediaCommandFIFO->items = calloc(count,sizeof(mediaCommandFIFO->items[0]));
    mediaCommandFIFO->count = count;
    mediaCommandFIFO->read_cur = 0;
    mediaCommandFIFO->write_cur = 0;
}

static MediaCommand* readMediaCommandFIFO(MediaCommandFIFO* mediaCommandFIFO) {
    if (mediaCommandFIFO->read_cur == mediaCommandFIFO->write_cur) return NULL;
    MediaCommand* data = &mediaCommandFIFO->items[mediaCommandFIFO->read_cur];
    mediaCommandFIFO->read_cur = (mediaCommandFIFO->read_cur + 1) % mediaCommandFIFO->count;
    return data;
}

static bool canWriteMediaCommandFIFO(MediaCommandFIFO* mediaCommandFIFO) {
    return !((mediaCommandFIFO->write_cur + 1) % mediaCommandFIFO->count == mediaCommandFIFO->read_cur);
}

static bool writeMediaCommandFIFO(MediaCommandFIFO* mediaCommandFIFO, MediaCommand* item) {
    if (!canWriteMediaCommandFIFO(mediaCommandFIFO)) return false;
    memcpy(&mediaCommandFIFO->items[mediaCommandFIFO->write_cur], item, sizeof(*item));
    mediaCommandFIFO->write_cur = (mediaCommandFIFO->write_cur + 1) % mediaCommandFIFO->count;
    return true;
}

// video frame storer

typedef struct {
    Frame* items;
    size_t count;
    atomic_int read_cur;
    atomic_int write_cur;
} VideoFramesFIFO;

static void createVideoFramesFIFO(size_t count, VideoFramesFIFO* videoFramesFIFO) {
    videoFramesFIFO->items = calloc(count,sizeof(videoFramesFIFO->items[0]));
    videoFramesFIFO->count = count;
    videoFramesFIFO->read_cur = 0;
    videoFramesFIFO->write_cur = 0;
}

static Frame* peekVideoFramesFIFO(VideoFramesFIFO* videoFramesFIFO) {
    if (videoFramesFIFO->read_cur == videoFramesFIFO->write_cur) return NULL;
    Frame* data = &videoFramesFIFO->items[videoFramesFIFO->read_cur];
    return data;
}

static Frame* readVideoFramesFIFO(VideoFramesFIFO* videoFramesFIFO) {
    if (videoFramesFIFO->read_cur == videoFramesFIFO->write_cur) return NULL;
    Frame* data = &videoFramesFIFO->items[videoFramesFIFO->read_cur];
    videoFramesFIFO->read_cur = (videoFramesFIFO->read_cur + 1) % videoFramesFIFO->count;
    return data;
}

static bool canWriteVideoFramesFIFO(VideoFramesFIFO* videoFramesFIFO) {
    return !((videoFramesFIFO->write_cur + 1) % videoFramesFIFO->count == videoFramesFIFO->read_cur);
}

static bool writeVideoFramesFIFO(VideoFramesFIFO* videoFramesFIFO, Frame* item) {
    if (!canWriteVideoFramesFIFO(videoFramesFIFO)) return false;
    memcpy(&videoFramesFIFO->items[videoFramesFIFO->write_cur], item, sizeof(*item));
    videoFramesFIFO->write_cur = (videoFramesFIFO->write_cur + 1) % videoFramesFIFO->count;
    return true;
}

//

static MediaCommandFIFO commandFIFO = {0};
static VideoFramesFIFO videoFramesFIFO = {0};

static struct SwrContext* swrContext;

static void clearVideoFramesFIFO(VideoFramesFIFO* fifo) {
    while (true) {
        Frame* frame = readVideoFramesFIFO(fifo);
        if (!frame) break;
        if (frame->video.data) {
            free(frame->video.data);
            frame->video.data = NULL;
        }
        memset(frame, 0, sizeof(*frame));
    }
    fifo->read_cur = 0;
    fifo->write_cur = 0;
}

static atomic_bool seeking = false;
static atomic_bool mediaPlaying = false;

static int commandWorkerFunc(void* arg){
    Media* media = (Media*)arg;
    MediaCommand* command = NULL;
    Frame frame = {0};
    while(true){
        command = readMediaCommandFIFO(&commandFIFO);
        if(command){
            if(command->type == MEDIA_COMMAND_SEEK){
                seeking = true;
                ffmpegMediaSeek(media,&frame,command->as.seek.frameTime);
                clearVideoFramesFIFO(&videoFramesFIFO);
                if(soundEngineInitialized() && media->audioStreamIndex != -1) soundEngineClear();
                seeking = false;
            }
            if(command->type == MEDIA_COMMAND_RESUME){
                mediaPlaying = true;
            }
            if(command->type == MEDIA_COMMAND_PAUSE){
                mediaPlaying = false;
            }
        }
    }
    return false;
}

static int workerFunc(void* arg){
    Media* media = (Media*)arg;
    Frame frame = {0};
    while(true){
        if(mediaPlaying && !seeking){
            if(ffmpegMediaGetFrame(media,&frame)){
                if(frame.type == FRAME_TYPE_AUDIO){
                    if(frame.audio.nb_samples == 0 || frame.audio.data == NULL) continue;
                    if(!soundEngineInitialized()) continue;
                    
                    const size_t out_size = frame.audio.nb_samples * soundEngineGetChannels() * sizeof(float);
                    float* out_data = malloc(out_size);
                    if (!out_data) continue;
                    
                    uint8_t* out_buf[1] = { (uint8_t*)out_data };
                    const uint8_t* in_buf[1] = { frame.audio.data };
                    
                    int out_samples = swr_convert(swrContext,
                        out_buf, frame.audio.nb_samples,
                        in_buf, frame.audio.nb_samples);
                    
                    if (out_samples > 0) {
                        SoundAudioFrame audioFrame = {
                            .data = out_data,
                            .numberSamples = out_samples
                        };
                        
                        while(!soundEngineCanEnqueueFrame()) {}
                        soundEngineEnqueueFrame(&audioFrame);
                        
                        // printf("Got Sound Frame! %.02f\n", frame.frameTime);
                    } else {
                        free(out_data);
                        fprintf(stderr, "Audio resampling failed: %d\n", out_samples);
                    }
                } else if(frame.type == FRAME_TYPE_VIDEO){
                    // printf("Got Video Frame! %.02f\n", frame.frameTime);
                    Frame enqueueFrame = frame;
                    enqueueFrame.video.data = malloc(enqueueFrame.video.width * enqueueFrame.video.height * sizeof(uint32_t));
                    memcpy(enqueueFrame.video.data, frame.video.data, enqueueFrame.video.width*enqueueFrame.video.height*sizeof(uint32_t));
                    while(!canWriteVideoFramesFIFO(&videoFramesFIFO)){}
                    writeVideoFramesFIFO(&videoFramesFIFO, &enqueueFrame);
                }
            }
        }
    }

    return 0;
}

void initializeFFmpegWorker(Media* media){
    createMediaCommandFIFO(30, &commandFIFO);
    createVideoFramesFIFO(100, &videoFramesFIFO);
    thrd_t worker = {0};

    if(soundEngineInitialized() && media->audioStreamIndex != -1){
        size_t formatInterlieved = media->audioCodecContext->sample_fmt;
    
             if(formatInterlieved == AV_SAMPLE_FMT_U8P) formatInterlieved = AV_SAMPLE_FMT_U8;
        else if(formatInterlieved == AV_SAMPLE_FMT_S16P) formatInterlieved = AV_SAMPLE_FMT_S16;
        else if(formatInterlieved == AV_SAMPLE_FMT_S32P) formatInterlieved = AV_SAMPLE_FMT_S32;
        else if(formatInterlieved == AV_SAMPLE_FMT_FLTP) formatInterlieved = AV_SAMPLE_FMT_FLT;
        else if(formatInterlieved == AV_SAMPLE_FMT_DBLP) formatInterlieved = AV_SAMPLE_FMT_DBL;
    
        int ret = swr_alloc_set_opts2(&swrContext,
            soundEngineGetChannels() == 2 ? &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO : &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO,
            AV_SAMPLE_FMT_FLT,
            soundEngineGetSampleRate(),
            &media->audioCodecContext->ch_layout,
            formatInterlieved,
            media->audioCodecContext->sample_rate,
            0,
            NULL);
        if (!swrContext || swr_init(swrContext) < 0) {
            fprintf(stderr, "Failed to initialize resampler\n");
            return;
        }
    }

    thrd_create(&worker, workerFunc, (void*)media);
    thrd_detach(worker);

    thrd_create(&worker, commandWorkerFunc, (void*)media);
    thrd_detach(worker);
}

void workerAskForPause(){
    MediaCommand command = {
        .type = MEDIA_COMMAND_PAUSE
    };

    while(!canWriteMediaCommandFIFO(&commandFIFO)) {}
    writeMediaCommandFIFO(&commandFIFO, &command);
}

void workerAskForResume(){
    MediaCommand command = {
        .type = MEDIA_COMMAND_RESUME
    };

    while(!canWriteMediaCommandFIFO(&commandFIFO)) {}
    writeMediaCommandFIFO(&commandFIFO, &command);
}

void workerAskForSeek(double frameTime){
    MediaCommand command = {
        .type = MEDIA_COMMAND_SEEK,
        .as.seek.frameTime = frameTime,
    };

    while(!canWriteMediaCommandFIFO(&commandFIFO)) {}
    writeMediaCommandFIFO(&commandFIFO, &command);
}

Frame* workerAskForVideoFrame(){
    return readVideoFramesFIFO(&videoFramesFIFO);
}

Frame* workerPeekVideoFrame(){
    return peekVideoFramesFIFO(&videoFramesFIFO);
}