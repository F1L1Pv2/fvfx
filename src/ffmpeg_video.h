#ifndef FVFX_FFMPEG_VIDEO
#define FVFX_FFMPEG_VIDEO

#include <vulkan/vulkan.h>
#include <stdbool.h>

bool ffmpegInit(char* filename, VkImage* imageOut, VkDeviceMemory* imageDeviceMemoryOut, VkImageView* imageViewOut, size_t* widthOut, size_t* heightOut);
bool ffmpegProcessFrame(double time);
bool ffmpegSeek(double time_seconds);
void ffmpegRender();

void ffmpegUninit();

double getDuration();
double getFrameTime();

#endif