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

    VkShaderModule vertexShader;
    VkPipeline defaultPipeline;
    VkPipelineLayout defaultPipelineLayout;

    size_t videoOutWidth;
    size_t videoOutHeight;
} Vulkanizer;

typedef struct {
    VulkanizerVfx* vfx;
    void* push_constants_data;
    size_t push_constants_size;
} VulkanizerVfxInstance;

typedef struct{
    VulkanizerVfxInstance* items;
    size_t count;
    size_t capacity;
} VulkanizerVfxInstances;

bool Vulkanizer_init(VkDevice deviceIN, VkDescriptorPool descriptorPoolIN, size_t outWidth, size_t outHeight, Vulkanizer* vulkanizer);
bool Vulkanizer_init_image_for_media(Vulkanizer* vulkanizer, size_t width, size_t height, VkImage* imageOut, VkDeviceMemory* imageMemoryOut, VkImageView* imageViewOut, size_t* imageStrideOut, VkDescriptorSet* descriptorSetOut, void* imageDataOut);
bool Vulkanizer_apply_vfx_on_frame_and_compose(VkCommandBuffer cmd, Vulkanizer* vulkanizer, VulkanizerVfxInstances* vfxInstances, VkImageView videoInView, void* videoInData, size_t videoInStride, VkDescriptorSet videoInDescriptorSet, Frame* frameIn, VkImageView composedOutView);
void Vulkanizer_reset_pool();

bool Vulkanizer_init_vfx(Vulkanizer* vulkanizer, const char* filename, VulkanizerVfx* outVfx);

bool createMyImage(VkImage* image, size_t width, size_t height, VkDeviceMemory* imageMemory, VkImageView* imageView, size_t* imageStride, void** imageMapped, VkImageUsageFlagBits imageUsage, VkMemoryPropertyFlagBits memoryProperty);

#endif