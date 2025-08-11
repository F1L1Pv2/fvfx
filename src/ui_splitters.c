#include "ui_splitters.h"
#include "engine/input.h"
#include "modules/spriteManager.h"
#include "gui_helpers.h"

#include "vulkan/vulkan.h"
#include "engine/vulkan_globals.h"

#include "stdbool.h"

void drawHorizontalSplitter(Rect boundingBox, bool *usingSplitter, float* splitterOffset){
    bool splitterHover = pointInsideRect(input.mouse_x, input.mouse_y, boundingBox);

    drawSprite((SpriteDrawCommand){
        .position = (vec2){boundingBox.x, boundingBox.y},
        .scale = (vec2){boundingBox.width, boundingBox.height},
        .albedo = splitterHover || *usingSplitter ? hex2rgb(0xFF909090) : hex2rgb(0xFF101010),
    });

    if(splitterHover && !*usingSplitter && input.keys[KEY_MOUSE_LEFT].justPressed) *usingSplitter = true;
    if(*usingSplitter && input.keys[KEY_MOUSE_LEFT].justReleased) *usingSplitter = false;
    if(*usingSplitter) *splitterOffset = swapchainExtent.width - input.mouse_x;
}

void drawVerticalSplitter(Rect boundingBox, bool *usingSplitter, float* splitterOffset){
    bool splitterHover = pointInsideRect(input.mouse_x, input.mouse_y, boundingBox);

    drawSprite((SpriteDrawCommand){
        .position = (vec2){boundingBox.x, boundingBox.y},
        .scale = (vec2){boundingBox.width, boundingBox.height},
        .albedo = splitterHover || *usingSplitter ? hex2rgb(0xFF909090) : hex2rgb(0xFF101010),
    });

    if(splitterHover && !*usingSplitter && input.keys[KEY_MOUSE_LEFT].justPressed) *usingSplitter = true;
    if(*usingSplitter && input.keys[KEY_MOUSE_LEFT].justReleased) *usingSplitter = false;
    if(*usingSplitter) *splitterOffset = swapchainExtent.height - input.mouse_y;
}