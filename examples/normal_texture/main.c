#include "engine/engine.h"
#include "engine/app.h"
#include "engine/vulkan_compileShader.h"
#include "engine/vulkan_createGraphicPipelines.h"
#include "engine/vulkan_globals.h"
#include "engine/vulkan_helpers.h"
#include "engine/vulkan_images.h"

#define NOB_STRIP_PREFIX
#include "nob.h"

VkPipeline pipeline;
VkPipelineLayout pipelineLayout;

VkDescriptorSet descriptorSet;
VkDescriptorSetLayout descriptorSetLayout;

#include "3rdparty/stb_image.h"

int main(){
    engineInit("normal_texture Example", 640, 480);

    String_Builder sb = {0};
    read_entire_file("assets/shaders/compiled/normal_texture.vert.spv", &sb);

    VkShaderModule vertexShader;
    if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count,&vertexShader)) return 1;

    sb.count = 0;
    read_entire_file("assets/shaders/compiled/normal_texture.frag.spv", &sb);

    VkShaderModule fragmentShader;
    if(!compileShaderFromBinary((uint32_t*)sb.items,sb.count,&fragmentShader)) return 1;

    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {0};
    descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorSetLayoutBinding.descriptorCount = 1;
    descriptorSetLayoutBinding.binding = 0;
    descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {0};
    descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.bindingCount  = 1;
    descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;

    if(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &descriptorSetLayout) != VK_SUCCESS){
        printf("ERROR\n");
        return 1;
    }

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;

    vkAllocateDescriptorSets(device,&descriptorSetAllocateInfo, &descriptorSet);

    if(!createGraphicPipeline((CreateGraphicsPipelineARGS){
        .vertexShader = vertexShader,
        .fragmentShader = fragmentShader,
        .descriptorSetLayoutCount = 1,
        .descriptorSetLayouts = &descriptorSetLayout,
        .pipelineOUT = &pipeline,
        .pipelineLayoutOUT = &pipelineLayout,
    })) return 1;


    VkImage image;
    VkImageView imageView;
    VkDeviceMemory imageMemory;

    int width,height;

    char* data = (char*)stbi_load("assets/test.png",(int*)&width,(int*)&height, NULL, 4);
    if(data == NULL){
        printf("ERROR: Couldn't load image\n");
        return false;
    }
    
    if(!createImage(width,height,VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &image, &imageMemory)){
        return false;
    }
    
    if(!sendDataToImage(image,data,width,width*sizeof(uint32_t),height)){
        return false;
    }
    
    if(!createImageView(image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, &imageView)){
        return false;
    }

    VkDescriptorImageInfo descriptorImageInfo = {0};
    descriptorImageInfo.sampler = samplerLinear;
    descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorImageInfo.imageView = imageView;

    VkWriteDescriptorSet writeDescriptorSet = {0};
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.dstSet = descriptorSet;
    writeDescriptorSet.dstBinding = 0;
    writeDescriptorSet.dstArrayElement = 0;
    writeDescriptorSet.pImageInfo = &descriptorImageInfo;

    vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);

    return engineStart();
}

bool afterResize(){
    return true;
}

bool update(float deltaTime){
    return true;
}

bool draw(){
    vkCmdBeginRenderingEX(cmd, (BeginRenderingEX){
        .colorAttachment = getSwapchainImageView(),
    });

    vkCmdSetViewport(cmd, 0, 1, &(VkViewport){
        .width = swapchainExtent.width,
        .height = swapchainExtent.height
    });
    
    vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
        .extent = swapchainExtent
    });

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&descriptorSet,0,NULL);


    vkCmdDraw(cmd,6,1,0,0);

    vkCmdEndRendering(cmd);

    return true;
}