#ifndef MODULES_BINDLESS_TEXTURES_MANAGER
#define MODULES_BINDLESS_TEXTURES_MANAGER

#include <stdbool.h>

#define NOB_STRIP_PREFIX
#include "nob.h"

#include "vulkan/vulkan.h"

bool initBindlessTextures(File_Paths paths);
int addBindlessTextureFromDisk(char* name);
int addBindlessTexture(char* name, char* data, size_t width, size_t height);
int getTextureID(char* name);

extern VkDescriptorSetLayout bindlessDescriptorSetLayout;
extern VkDescriptorSet bindlessDescriptorSet;

#endif