#include <stdio.h>

#include "ffmpeg_media.h"
#include "ffmpeg_media_render.h"

int main(int argc, char** argv){
    if(argc < 2){
        fprintf(stderr, "Provide filename!\n");
        return 1;
    }
    printf("Hjello Freunder!\n");

    Media media = {0};
    if(!ffmpegMediaInit(argv[1], &media)){
        fprintf(stderr, "Couldn't initialize ffmpeg media!\n");
        return 1;
    }
    MediaRenderContext renderContext = {0};
    if(!ffmpegMediaRenderInit(&media, "output.mp4", &renderContext)){
        fprintf(stderr, "Couldn't initialize ffmpeg media renderer!\n");
        return 1;
    }

    Frame frame = {0};
    while(ffmpegMediaGetFrame(&media, &frame)){
        ffmpegMediaRenderPassFrame(&renderContext, &frame);
    }
    ffmpegMediaRenderFinish(&renderContext);

    printf("Finished rendering!\n");

    return 0;
}