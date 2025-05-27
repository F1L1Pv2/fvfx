#include <stdio.h>
#include <stdbool.h>

#define NOB_STRIP_PREFIX
#include "nob.h"

#include "vulkan/vulkan.h"

#include "vulkan_globals.h"
#include "vulkan_initPipelineLayout.h"

VkPipelineLayout pipelineLayout;

bool initPipelineLayout(size_t pushConstantsSize){
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {0};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.pNext = NULL;
    pipelineLayoutCreateInfo.flags = 0;
    pipelineLayoutCreateInfo.setLayoutCount = 0;
    pipelineLayoutCreateInfo.pSetLayouts = NULL;

    if(pushConstantsSize > 0){
        VkPushConstantRange pushConstantRange = {0};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.offset = 0;
        pushConstantRange.size = pushConstantsSize;

        pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    }else{
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = NULL;
    }

    VkResult result = vkCreatePipelineLayout(device,&pipelineLayoutCreateInfo,NULL,&pipelineLayout);
    if(result != VK_SUCCESS){
        printf("Couldn't create pipeline layout\n");
        return false;
    }

    return true;
}