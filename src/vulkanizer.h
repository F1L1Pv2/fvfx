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
    VkDescriptorSet vfxDescriptorSetImage1;
    VkDescriptorSet vfxDescriptorSetImage2;

    VkShaderModule vertexShader;
    VkPipeline defaultPipeline;
    VkPipelineLayout defaultPipelineLayout;

    int currentImage;

    VkImage videoOut1Image;
    VkDeviceMemory videoOut1ImageMemory;
    VkImageView videoOut1ImageView;
    size_t videoOut1ImageStride;
    void* videoOut1ImageMapped;

    VkImage videoOut2Image;
    VkDeviceMemory videoOut2ImageMemory;
    VkImageView videoOut2ImageView;
    size_t videoOut2ImageStride;
    void* videoOut2ImageMapped;

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

bool Vulkanizer_init(VkDevice deviceIN, VkDescriptorPool descriptorPoolIN, Vulkanizer* vulkanizer);
bool Vulkanizer_init_image_for_media(size_t width, size_t height, VkImage* imageOut, VkDeviceMemory* imageMemoryOut, VkImageView* imageViewOut, size_t* imageStrideOut, void* imageDataOut);
bool Vulkanizer_init_output_image(Vulkanizer* vulkanizer, size_t outWidth, size_t outHeight);
bool Vulkanizer_apply_vfx_on_frame(VkCommandBuffer cmd, Vulkanizer* vulkanizer, VulkanizerVfxInstances* vfxInstances, VkImageView videoInView, void* videoInData, size_t videoInStride, Frame* frameIn);
void Vulkanizer_output_vfx_to_frame(Vulkanizer* vulkanizer, void* outData);

bool Vulkanizer_init_vfx(Vulkanizer* vulkanizer, const char* filename, VulkanizerVfx* outVfx);

#endif