#ifndef FVFX_MY_PROJECT
#define FVFX_MY_PROJECT

#include "project.h"
#include "vulkanizer.h"
#include <libavutil/audio_fifo.h>
#include "arena_alloc.h"

typedef struct MyMedia MyMedia;

struct MyMedia{
    Media media;
    bool hasAudio;
    bool hasVideo;

    VkImage mediaImage;
    VkDeviceMemory mediaImageMemory;
    VkImageView mediaImageView;
    size_t mediaImageStride;
    void* mediaImageData;
    VkDescriptorSet mediaDescriptorSet;
    double duration;
    MyMedia* next;
};

typedef struct MyVfx MyVfx;

struct MyVfx{
    VulkanizerVfx vfx;
    MyVfx* next;
};

enum {
    GET_FRAME_ERR = 1,
    GET_FRAME_FINISHED,
    GET_FRAME_SKIP,
    GET_FRAME_NEXT_MEDIA,
};

typedef struct{
    double localTime;
    double checkDuration;
    size_t currentSlice;
    size_t currentMediaIndex;
    size_t video_skip_count;
    size_t times_to_catch_up_target_framerate;
    int64_t lastVideoPts;
} GetVideoFrameArgs;

typedef struct MyLayer MyLayer;

struct MyLayer{
    MyMedia* myMedias;
    AVAudioFifo* audioFifo;
    Frame frame;
    GetVideoFrameArgs args;
    bool finished;
    double volume;
    double pan;
    MyLayer* next;
};

typedef struct{
    MyLayer* myLayers;
    enum AVSampleFormat myLayers_fifo_fmt;
    AVChannelLayout myLayers_fifo_ch_layout;
    size_t myLayers_fifo_frame_size;
    MyVfx* myVfxs;
    double time;
    double duration;
} MyProject;

enum {
    PROCESS_PROJECT_CONTINUE = 0,
    PROCESS_PROJECT_FINISHED
};

bool prepare_project(Project* project, MyProject* myProject, Vulkanizer* vulkanizer, enum AVSampleFormat expectedSampleFormat, size_t fifo_size, ArenaAllocator* aa);
int process_project(VkCommandBuffer cmd, Project* project, MyProject* myProject, Vulkanizer* vulkanizer, VulkanizerVfxInstances* vulkanizerVfxInstances, void* push_constants_buf, VkImageView outComposedImageView, bool* enoughSamplesOUT);
bool project_seek(Project* project, MyProject* myProject, double time_seconds);
void project_uninit(Vulkanizer* vulkanizer, MyProject* myProject, ArenaAllocator* aa);

#endif