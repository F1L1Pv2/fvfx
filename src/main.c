#include <stdio.h>

#include "ffmpeg_video.h"
#include "ffmpeg_video_render.h"

int main(int argc, char** argv){
    if(argc < 2){
        fprintf(stderr, "Provide filename!\n");
        return 1;
    }
    printf("Hjello Freunder!\n");

    Video video = {0};
    if(!ffmpegVideoInit(argv[1], &video)){
        fprintf(stderr, "Couldn't initialize ffmpeg video!\n");
        return 1;
    }
    VideoRenderContext renderContext = {0};
    if(!ffmpegVideoRenderInit(&video, "output.mp4", &renderContext)){
        fprintf(stderr, "Couldn't initialize ffmpeg video renderer!\n");
        return 1;
    }

    Frame frame = {0};
    while(ffmpegVideoGetFrame(&video, &frame)){
        ffmpegVideoRenderPassFrame(&renderContext, &frame);
        if(frame.type == FRAME_TYPE_AUDIO){
            free(frame.audio.data);
        }
    }
    ffmpegVideoRenderFinish(&renderContext);

    printf("Finished rendering!\n");

    return 0;
}