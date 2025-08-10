#ifndef FVFX_FFMPEG_WORKER
#define FVFX_FFMPEG_WORKER

#include "ffmpeg_media.h"
void initializeFFmpegWorker(Media* media);

void workerAskForPause();
void workerAskForResume();
void workerAskForSeek(double frameTime);
Frame* workerAskForVideoFrame();
Frame* workerPeekVideoFrame();

#endif