#include <stdio.h>
#include <stdbool.h>

#define NOB_STRIP_PREFIX
#include "nob.h"

#include "vulkan/vulkan.h"

#include "vulkan_globals.h"
#include "vulkan_initDescriptorSet.h"

VkDescriptorSetLayout descriptorSetLayout;
VkDescriptorSet bindlessDescriptorSet;

bool initDescriptorSet(){
    VkDescriptorBindingFlags descriptorBindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {0};
    descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorSetLayoutBinding.descriptorCount = k_max_bindless_resources;
    descriptorSetLayoutBinding.binding = 0;
    descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;
    descriptorSetLayoutBinding.pImmutableSamplers = NULL;

    VkDescriptorSetLayoutBindingFlagsCreateInfo descriptorSetLayoutBindingFlagsCreateInfo = {0};
    descriptorSetLayoutBindingFlagsCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    descriptorSetLayoutBindingFlagsCreateInfo.pNext = NULL;
    descriptorSetLayoutBindingFlagsCreateInfo.bindingCount = 1;
    descriptorSetLayoutBindingFlagsCreateInfo.pBindingFlags = &descriptorBindingFlags;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {0};
    descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.pNext = &descriptorSetLayoutBindingFlagsCreateInfo;
    descriptorSetLayoutCreateInfo.bindingCount = 1;
    descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;
    descriptorSetLayoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;

    VkResult result = vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &descriptorSetLayout);
    if(result != VK_SUCCESS){
        printf("ERROR: Couldn't create descriptor set layout\n");
        return false;
    }

    VkDescriptorSetVariableDescriptorCountAllocateInfo count_info = {0};
    count_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    count_info.descriptorSetCount = 1;
    uint32_t max_binding = k_max_bindless_resources - 1;
    count_info.pDescriptorCounts = &max_binding;

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0}; 
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.pNext = &count_info;
    descriptorSetAllocateInfo.descriptorPool = descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;

    result = vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &bindlessDescriptorSet);
    if(result != VK_SUCCESS){
        printf("ERROR: Couldn't allocate descriptor set\n");
        return false;
    }

    return true;
}