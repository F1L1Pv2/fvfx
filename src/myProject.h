#ifndef FVFX_MY_PROJECT
#define FVFX_MY_PROJECT

#include "project.h"
#include "vulkanizer.h"
#include <libavutil/audio_fifo.h>
#include "string_alloc.h"

typedef struct{
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
} MyMedia;

typedef struct{
    MyMedia* items;
    size_t count;
    size_t capacity;
} MyMedias;

typedef struct{
    VulkanizerVfx* items;
    size_t count;
    size_t capacity;
} MyVfxs;

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

typedef struct{
    MyMedias myMedias;
    AVAudioFifo* audioFifo;
    Frame frame;
    GetVideoFrameArgs args;
    bool finished;
} MyLayer;

typedef struct{
    MyLayer* items;
    size_t count;
    size_t capacity;

    enum AVSampleFormat fifo_fmt;
    AVChannelLayout fifo_ch_layout;
    size_t fifo_frame_size;
} MyLayers;

typedef struct{
    MyLayers myLayers;
    MyVfxs myVfxs;
    double time;
    double duration;
} MyProject;

enum {
    PROCESS_PROJECT_CONTINUE = 0,
    PROCESS_PROJECT_FINISHED
};

bool prepare_project(Project* project, MyProject* myProject, Vulkanizer* vulkanizer, enum AVSampleFormat expectedSampleFormat, size_t fifo_size);
int process_project(VkCommandBuffer cmd, Project* project, MyProject* myProject, Vulkanizer* vulkanizer, VulkanizerVfxInstances* vulkanizerVfxInstances, void* push_constants_buf, VkImageView outComposedImageView, bool* enoughSamplesOUT);
bool project_seek(Project* project, MyProject* myProject, double time_seconds);
void project_uninit(Vulkanizer* vulkanizer, MyProject* myProject, StringAllocator* sa);

#endif