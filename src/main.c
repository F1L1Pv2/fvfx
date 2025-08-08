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
    Frame frame = {0};
    
    if(!ffmpegVideoInit(argv[1], &video)){
        fprintf(stderr, "Couldn't initialize ffmpeg video!\n");
        return 1;
    }

    ffmpegVideoGetFrame(&video, &frame);
    VideoRenderContext renderContext = {0};

    ffmpegVideoRenderInit(&video, "output.mp4", &renderContext);
    ffmpegVideoRenderPassFrame(&renderContext, &frame);

    while(ffmpegVideoGetFrame(&video, &frame)){
        ffmpegVideoRenderPassFrame(&renderContext, &frame);
    }

    ffmpegVideoRenderFinish(&renderContext);

    printf("Finished rendering!\n");

    return 0;
}