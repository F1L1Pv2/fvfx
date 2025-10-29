#include "fvfx_helper.h"
#include "ffmpeg_helper.h"
#include "ll.h"

void mix_all_layers(
    uint8_t** composedAudioBuf,   // [out] output buffer (already cleared)
    uint8_t** tempAudioBuf,       // [in] temp buffer
    MyLayer* myLayers,            // linked list of layers
    int out_audio_frame_size,     // number of frames to produce this iteration
    enum AVSampleFormat out_audio_format, // output sample format
    Project* project              // holds global mix settings (e.g., stereo flag)
) {
    for (MyLayer* myLayer = myLayers; myLayer != NULL; myLayer = myLayer->next) {

        if (!myLayer->audioFifo)
            continue;

        int available = av_audio_fifo_size(myLayer->audioFifo);
        int toRead = FFMIN(available, out_audio_frame_size);

        av_samples_set_silence(
            tempAudioBuf,
            0,
            out_audio_frame_size,
            project->settings.stereo ? 2 : 1,
            out_audio_format
        );

        int read = 0;
        if (available > 0) {
            read = av_audio_fifo_read(myLayer->audioFifo, (void**)tempAudioBuf, toRead);
            if (read < 0)
                continue;
        }

        if (read < out_audio_frame_size)
            read = out_audio_frame_size;

        MyMedia* myMedia = ll_at(myLayer->myMedias, myLayer->args.currentMediaIndex);

        bool conditionalMix =
            (myLayer->finished) ||
            (myLayer->args.currentMediaIndex == EMPTY_MEDIA) ||
            (myLayer->args.currentMediaIndex != EMPTY_MEDIA && !myMedia->hasAudio);

        mix_audio(
            composedAudioBuf,
            tempAudioBuf,
            read,
            project->settings.stereo ? 2 : 1,
            out_audio_format,
            myLayer->volume,
            myLayer->pan
        );

        if (conditionalMix)
            continue;
    }
}