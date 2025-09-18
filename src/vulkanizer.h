#ifndef FVFX_VULKANIZER
#define FVFX_VULKANIZER

#include "ffmpeg_media.h"
#include "vulkan/vulkan.h"
#include <stddef.h>
#include "shader_utils.h"

typedef struct{
    VfxModule module;
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
} VulkanizerVfx;

typedef struct{
    VulkanizerVfx** items;
    size_t count;
    size_t capacity;
} VulkanizerVfxsRef;

typedef struct{
    VkDescriptorSetLayout vfxDescriptorSetLayout;
    VkDescriptorSet vfxDescriptorSet;

    VkShaderModule vertexShader;
    VulkanizerVfx vfx;

    VkImage videoOutImage;
    VkDeviceMemory videoOutImageMemory;
    VkImageView videoOutImageView;
    size_t videoOutImageStride;
    void* videoOutImageMapped;

    size_t videoOutWidth;
    size_t videoOutHeight;
} Vulkanizer;

bool Vulkanizer_init(Vulkanizer* vulkanizer);
bool Vulkanizer_init_image_for_media(size_t width, size_t height, VkImage* imageOut, VkDeviceMemory* imageMemoryOut, VkImageView* imageViewOut, size_t* imageStrideOut, void* imageDataOut);
bool Vulkanizer_init_output_image(Vulkanizer* vulkanizer, size_t outWidth, size_t outHeight);
bool Vulkanizer_apply_vfx_on_frame(Vulkanizer* vulkanizer, VkImageView videoInView, void* videoInData, size_t videoInStride, Frame* frameIn, void* outData);

bool Vulkanizer_init_vfx(Vulkanizer* vulkanizer, const char* filename, VulkanizerVfx* outVfx);

#endif