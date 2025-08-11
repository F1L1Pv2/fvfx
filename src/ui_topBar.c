#include "ui_topBar.h"
#include "gui_helpers.h"
#include "modules/spriteManager.h"
#include "ffmpeg_media.h"

extern double Time;
extern Media media;
extern bool rendering;

#define TOP_BAR_FONT_SIZE 16

void drawTopBar(Rect topBarRect){
    drawSprite((SpriteDrawCommand){
        .position = (vec2){topBarRect.x, topBarRect.y},
        .scale = (vec2){topBarRect.width, topBarRect.height},
        .albedo = hex2rgb(0xFF454545),
    });
    drawSprite((SpriteDrawCommand){
        .position = (vec2){topBarRect.x, topBarRect.y},
        .scale = (vec2){topBarRect.width, topBarRect.height - 1},
        .albedo = hex2rgb(0xFF181818),
    });

    drawText(rendering ? "FVFX RENDERING!" : "FVFX", 0xFFFFFF, TOP_BAR_FONT_SIZE, (Rect){
        .x = topBarRect.x+10,
        .y = topBarRect.y+3,
    });

    char text[256];

    sprintf(text, "%.2fs/%.2fs", Time, media.duration);

    drawText(text, 0xFFFFFF, TOP_BAR_FONT_SIZE, (Rect){
        .x = topBarRect.x + topBarRect.width / 2 - measureText(text,TOP_BAR_FONT_SIZE) / 2,
        .y = topBarRect.y+3,
    });
}