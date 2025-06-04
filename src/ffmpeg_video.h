#ifndef FVFX_FFMPEG_VIDEO
#define FVFX_FFMPEG_VIDEO

#include <vulkan/vulkan.h>
#include <stdbool.h>

bool loadFrame(char* filename, VkImage* imageOut, VkDeviceMemory* imageDeviceMemoryOut, VkImageView* imageViewOut, size_t* widthOut, size_t* heightOut);

#endif