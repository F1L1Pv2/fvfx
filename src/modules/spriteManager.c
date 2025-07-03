#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define NOB_STRIP_PREFIX
#include "nob.h"

#include "vulkan/vulkan.h"
#include "engine/vulkan_globals.h"
#include "engine/vulkan_buffer.h"

#include "spriteManager.h"
#include "gui_helpers.h"

typedef struct {
    SpriteDrawCommand* items;
    size_t count;
    size_t capacity;
} SpriteDrawCommands;

static SpriteDrawCommands spriteDrawQueue = {0};

VkBuffer spriteDrawBuffer;
VkDeviceMemory spriteDrawMemory;

bool initSpriteManager(){
    if(!createBuffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,sizeof(SpriteDrawCommand)*MAX_SPRITE_COUNT,&spriteDrawBuffer,&spriteDrawMemory)) return false;
    return true;
}

void drawSprite(SpriteDrawCommand cmd){
    // tbh i dont know how much this will impact performance if it will be a bottleneck then i can remove it
    if(!rectIntersectsRect((Rect){
        .x = cmd.position.x,
        .y = cmd.position.y,
        .width = cmd.scale.x,
        .height = cmd.scale.y,
    }, (Rect){
        .x = 0,
        .y = 0,
        .width = swapchainExtent.width,
        .height = swapchainExtent.height
    })) return;

    if(spriteDrawQueue.count < MAX_SPRITE_COUNT){
        if(cmd.size.x == 0 && cmd.size.y == 0){
            cmd.size.x = 1;
            cmd.size.y = 1;
        }
        da_append(&spriteDrawQueue, cmd);
    }
}

void renderSprites(){
    // its assumes that everything is attached correctly

    if(spriteDrawQueue.count > 0){
        transferDataToMemory(spriteDrawMemory,spriteDrawQueue.items,0,sizeof(spriteDrawQueue.items[0])*spriteDrawQueue.count);
        vkCmdDraw(cmd,6,spriteDrawQueue.count,0,0);
        spriteDrawQueue.count = 0;
    }

}