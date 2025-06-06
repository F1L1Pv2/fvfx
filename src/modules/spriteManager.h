#ifndef MODULES_SPRITE_MANAGER
#define MODULES_SPRITE_MANAGER

#define MAX_SPRITE_COUNT 1024

#include <stdbool.h>

#include "vulkan/vulkan.h"

#include "modules/gmath.h"

typedef struct {
    mat4 transform;
    uint32_t textureID;
    vec3 albedo;
    vec2 offset;
    vec2 size;
} SpriteDrawCommand;

bool initSpriteManager();

void drawSprite(SpriteDrawCommand cmd);

void renderSprites();

extern VkBuffer spriteDrawBuffer;
extern VkDeviceMemory spriteDrawMemory;


#endif