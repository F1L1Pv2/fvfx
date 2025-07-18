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

// static SpriteDrawCommands spriteDrawQueue = {0};

typedef struct{
    float scissorX, scissorY, scissorWidth, scissorHeight;
    SpriteDrawCommands drawQueue;
} FullDrawQueue;

typedef struct{
    FullDrawQueue* items;
    size_t count;
    size_t capacity;
} FullDrawQueues;

static FullDrawQueues spriteDrawQueues = {0};
size_t currentDrawQueue = 0;

VkBuffer spriteDrawBuffer;
VkDeviceMemory spriteDrawMemory;

SpriteDrawCommands* customDrawQueue;
bool usingCustomDrawQueue = false;

bool initSpriteManager(){
    if(!createBuffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,sizeof(SpriteDrawCommand)*MAX_SPRITE_COUNT,&spriteDrawBuffer,&spriteDrawMemory)) return false;
    da_append(&spriteDrawQueues, ((FullDrawQueue){0}));
    return true;
}

void beginScissor(float x, float y, float width, float height){
    if(currentDrawQueue + 1 >= spriteDrawQueues.count){
        da_append(&spriteDrawQueues, ((FullDrawQueue){.scissorX = x, .scissorY = y, .scissorWidth = width, .scissorHeight = height, .drawQueue = ((SpriteDrawCommands){0})}));
    }else{        
        spriteDrawQueues.items[currentDrawQueue + 1].scissorX = x,
        spriteDrawQueues.items[currentDrawQueue + 1].scissorY = y,
        spriteDrawQueues.items[currentDrawQueue + 1].scissorWidth = width,
        spriteDrawQueues.items[currentDrawQueue + 1].scissorHeight = height,
        spriteDrawQueues.items[currentDrawQueue + 1].drawQueue.count = 0;
    }
    currentDrawQueue++;
}

void endScissor(){
    if(currentDrawQueue + 1 >= spriteDrawQueues.count){
        da_append(&spriteDrawQueues, ((FullDrawQueue){0}));
    }else{
        spriteDrawQueues.items[currentDrawQueue + 1].scissorX = 0,
        spriteDrawQueues.items[currentDrawQueue + 1].scissorY = 0,
        spriteDrawQueues.items[currentDrawQueue + 1].scissorWidth = 0,
        spriteDrawQueues.items[currentDrawQueue + 1].scissorHeight = 0,
        spriteDrawQueues.items[currentDrawQueue + 1].drawQueue.count = 0;
    }
    currentDrawQueue++;
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

    SpriteDrawCommands* usedSpriteDrawQueue = usingCustomDrawQueue ? customDrawQueue : &spriteDrawQueues.items[currentDrawQueue].drawQueue;

    if(usedSpriteDrawQueue->count < MAX_SPRITE_COUNT){
        if(cmd.size.x == 0 && cmd.size.y == 0){
            cmd.size.x = 1;
            cmd.size.y = 1;
        }
        da_append(usedSpriteDrawQueue, cmd);
    }
}

//set to null to set to default draw queue
void redirectDrawSprites(SpriteDrawCommands* drawQueue){
    if(drawQueue != NULL){
        customDrawQueue = drawQueue;
        usingCustomDrawQueue = true;
    }else{
        usingCustomDrawQueue = false;
    }
}

void drawSprites(SpriteDrawCommands* cmds){
    da_append_many(&spriteDrawQueues.items[currentDrawQueue].drawQueue, cmds->items, cmds->count);
}

void renderSprites(float renderWidth, float renderHeight){
    // its assumes that everything is attached correctly

    size_t vertexOffset = 0;

    for(size_t i = 0; i < spriteDrawQueues.count; i++){
        SpriteDrawCommands* spriteDrawQueue = &spriteDrawQueues.items[i].drawQueue;
    
        if(spriteDrawQueues.items[i].scissorWidth == 0 && spriteDrawQueues.items[i].scissorHeight == 0){
            vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
                .extent = (VkExtent2D){.width = renderWidth, .height = renderHeight},
                .offset = (VkOffset2D){.x = 0, .y = 0},
            });
        }else{
            vkCmdSetScissor(cmd, 0, 1, &(VkRect2D){
                .extent = (VkExtent2D){.width = spriteDrawQueues.items[i].scissorWidth, .height = spriteDrawQueues.items[i].scissorHeight},
                .offset = (VkOffset2D){.x = spriteDrawQueues.items[i].scissorX, .y = spriteDrawQueues.items[i].scissorY},
            });
        }
    
        if(spriteDrawQueue->count > 0){
            transferDataToMemory(spriteDrawMemory,spriteDrawQueue->items,sizeof(spriteDrawQueue->items[0])*vertexOffset,sizeof(spriteDrawQueue->items[0])*spriteDrawQueue->count);
            vkCmdDraw(cmd,6,spriteDrawQueue->count,0,vertexOffset);
            vertexOffset += spriteDrawQueue->count;
            spriteDrawQueue->count = 0;
        }
    }

    currentDrawQueue = 0;
}