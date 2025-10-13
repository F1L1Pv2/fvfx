#ifndef FVFX_MY_PROJECT
#define FVFX_MY_PROJECT

#include "project.h"
#include "vulkanizer.h"
#include <libavutil/audio_fifo.h>

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

enum {
    PROCESS_PROJECT_CONTINUE = 0,
    PROCESS_PROJECT_FINISHED
};

void VfxInstance_Update(MyVfxs* myVfxs, VfxInstance* instance, double currentTime, void* push_constants_data);
bool prepare_project(Project* project, Vulkanizer* vulkanizer, MyLayers* myLayers, MyVfxs* myVfxs, enum AVSampleFormat expectedSampleFormat, size_t fifo_size);
bool init_my_project(Project* project, MyLayers* myLayers);
int process_project(VkCommandBuffer cmd, Project* project, Vulkanizer* vulkanizer, MyLayers* myLayers, MyVfxs* myVfxs, VulkanizerVfxInstances* vulkanizerVfxInstances, double projectTime, void* push_constants_buf, VkImageView outComposedImageView, bool* enoughSamplesOUT);

#endif