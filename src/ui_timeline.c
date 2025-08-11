#include "ui_timeline.h"
#include "gui_helpers.h"
#include "modules/spriteManager.h"
#include "ffmpeg_media.h"
#include "ffmpeg_worker.h"
#include "engine/input.h"

void drawTimeline(Rect timelineContainer, double* Time, Media* media){
    float percent = *Time / media->duration;

    float cursorWidth = 1;

    drawSprite((SpriteDrawCommand){
        .position = (vec2){timelineContainer.x, timelineContainer.y},
        .scale = (vec2){timelineContainer.width, timelineContainer.height},
        .albedo = hex2rgb(0xFF181818),
    });
    
    drawSprite((SpriteDrawCommand){
        .position = (vec2){floor(timelineContainer.x+percent*timelineContainer.width+cursorWidth/2),timelineContainer.y},
        .scale = (vec2){cursorWidth, timelineContainer.height},
        .albedo = hex2rgb(0xFFFF0000),
    });

    if(pointInsideRect(input.mouse_x, input.mouse_y, timelineContainer) && input.keys[KEY_MOUSE_LEFT].justPressed){
        *Time = ((float)input.mouse_x - timelineContainer.x) * media->duration / timelineContainer.width;
        workerAskForSeek(*Time);
        //TODO: add back drawing pr3eview during not playing
    }
}