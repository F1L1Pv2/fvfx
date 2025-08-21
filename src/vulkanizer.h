#ifndef FVFX_VULKANIZER
#define FVFX_VULKANIZER

#include "ffmpeg_media.h"
#include "vulkan/vulkan.h"

typedef struct{
    VkDescriptorSetLayout vfxDescriptorSetLayout;
    VkDescriptorSet outDescriptorSet;

    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;

    VkImage videoInImage;
    VkDeviceMemory videoInImageMemory;
    VkImageView videoInImageView;
    size_t videoInImageStride;
    void* videoInImageMapped;

    VkImage videoOutImage;
    VkDeviceMemory videoOutImageMemory;
    VkImageView videoOutImageView;
    size_t videoOutImageStride;
    void* videoOutImageMapped;
} Vulkanizer;

bool Vulkanizer_init(Vulkanizer* vulkanizer);
bool Vulkanizer_init_images(Vulkanizer* vulkanizer, size_t width, size_t height);
bool Vulkanizer_apply_vfx_on_frame(Vulkanizer* vulkanizer, Frame* frame);

#endif