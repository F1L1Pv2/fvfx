#ifndef TRIEX_VULKAN_CREATE_GRAPHIC_PIPELINES
#define TRIEX_VULKAN_CREATE_GRAPHIC_PIPELINES

#include <stdbool.h>
#include <vulkan/vulkan.h>

typedef struct {
    VkShaderModule vertexShader;
    VkShaderModule fragmentShader;
    size_t pushConstantsSize;
    VkPipeline* pipelineOUT;
    VkPipelineLayout* pipelineLayoutOUT;
    size_t descriptorSetLayoutCount;
    VkDescriptorSetLayout* descriptorSetLayouts;
} CreateGraphicsPipelineARGS;

bool createGraphicPipeline(CreateGraphicsPipelineARGS args);

#endif