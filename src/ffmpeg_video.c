#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "vulkan/vulkan.h"

#include "engine/vulkan_globals.h"
#include "ffmpeg_video.h"
#include "engine/vulkan_helpers.h"
#include "engine/vulkan_images.h"

#include <malloc.h>

bool loadFrame(char* filename, VkImage* imageOut, VkDeviceMemory* imageDeviceMemoryOut, VkImageView* imageViewOut, size_t* widthOut, size_t* heightOut){
    const int dummy_witdh = 1920;
    const int dummy_height = 1080;

    *widthOut = dummy_witdh;
    *heightOut = dummy_height;

    uint32_t* data = malloc(dummy_witdh * dummy_height * sizeof(uint32_t));

    for(int i = 0; i < dummy_witdh*dummy_height; i++){
        data[i] = 0xFF0000FF;
    }

    if(!createImage(dummy_witdh,dummy_height,VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, imageOut, imageDeviceMemoryOut)){
        return false;
    }
    
    if(!sendDataToImage(*imageOut,data,dummy_witdh,dummy_witdh*sizeof(uint32_t),dummy_height)){
        return false;
    }
    
    if(!createImageView(*imageOut, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, imageViewOut)){
        return false;
    }

    return true;
}