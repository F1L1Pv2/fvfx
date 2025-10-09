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
    int currentImage;
    
    VkImage image1;
    VkDeviceMemory imageMemory1;
    VkImageView imageView1;
    size_t imageStride1;
    void* imageMapped1;
    VkDescriptorSet descriptorSet1;
    
    VkImage image2;
    VkDeviceMemory imageMemory2;
    VkImageView imageView2;
    size_t imageStride2;
    void* imageMapped2;
    VkDescriptorSet descriptorSet2;
} VulkanizerImagesOut;

typedef struct{
    VkDescriptorSetLayout vfxDescriptorSetLayout;
    VkDescriptorSet vfxDescriptorSet;

    VkShaderModule vertexShader;
    VkPipeline defaultPipeline;
    VkPipelineLayout defaultPipelineLayout;

    VulkanizerImagesOut vfxImagesOut;

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
bool Vulkanizer_init_image_for_media(size_t width, size_t height, VkImage* imageOut, VkDeviceMemory* imageMemoryOut, VkImageView* imageViewOut, size_t* imageStrideOut, void* imageDataOut);
bool Vulkanizer_apply_vfx_on_frame_and_compose(VkCommandBuffer cmd, Vulkanizer* vulkanizer, VulkanizerVfxInstances* vfxInstances, VkImageView videoInView, void* videoInData, size_t videoInStride, Frame* frameIn, VkImageView composedOutView);

bool Vulkanizer_init_vfx(Vulkanizer* vulkanizer, const char* filename, VulkanizerVfx* outVfx);

void transitionMyImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlagBits oldStage, VkPipelineStageFlagBits newStage);
bool createMyImage(VkImage* image, size_t width, size_t height, VkDeviceMemory* imageMemory, VkImageView* imageView, size_t* imageStride, void** imageMapped, VkImageUsageFlagBits imageUsage, VkMemoryPropertyFlagBits memoryProperty);

#endif