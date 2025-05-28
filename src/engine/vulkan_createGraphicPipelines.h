#ifndef TRIEX_VULKAN_CREATE_GRAPHIC_PIPELINES
#define TRIEX_VULKAN_CREATE_GRAPHIC_PIPELINES

#include <stdbool.h>
#include <vulkan/vulkan.h>

bool createGraphicPipeline(VkShaderModule vertexShader, VkShaderModule fragmentShader, size_t pushConstantsSize, VkPipeline* pipeline, VkPipelineLayout* pipelineLayout);

#endif