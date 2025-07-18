#ifndef MODULES_SPRITE_MANAGER
#define MODULES_SPRITE_MANAGER

#define MAX_SPRITE_COUNT 1024

#include <stdbool.h>

#include "vulkan/vulkan.h"

#include "modules/gmath.h"

typedef struct {
    vec2 position;
    vec2 scale;
    uint32_t textureIDEffects;
    vec3 albedo;
    vec2 offset;
    vec2 size;
} SpriteDrawCommand;

typedef struct {
    SpriteDrawCommand* items;
    size_t count;
    size_t capacity;
} SpriteDrawCommands;


bool initSpriteManager();

void beginScissor(float x, float y, float width, float height);
void endScissor();

void drawSprite(SpriteDrawCommand cmd);

void redirectDrawSprites(SpriteDrawCommands* drawQueue);
void drawSprites(SpriteDrawCommands* cmds);

void renderSprites(float renderWidth, float renderHeight);

extern VkBuffer spriteDrawBuffer;
extern VkDeviceMemory spriteDrawMemory;


#endif