#include <ffmpeg_helper.h>
#include <libavutil/samplefmt.h>
#include <libavutil/audio_fifo.h>
#include <stddef.h>
#include <stdint.h>

void mix_audio(uint8_t** base, uint8_t** added, size_t nb_samples, size_t num_channels, enum AVSampleFormat sample_fmt) {
    if (sample_fmt != AV_SAMPLE_FMT_FLTP) {
        // Only FLTP supported for now
        return;
    }

    for (size_t ch = 0; ch < num_channels; ch++) {
        float *base_ch  = (float*) base[ch];
        float *added_ch = (float*) added[ch];

        for (size_t i = 0; i < nb_samples; i++) {
            float mixed = base_ch[i] + added_ch[i];

            // Clamp to [-1.0, 1.0]
            if (mixed > 1.0f)  mixed = 1.0f;
            if (mixed < -1.0f) mixed = -1.0f;

            base_ch[i] = mixed;
        }
    }
}

int av_audio_fifo_add_silence(AVAudioFifo *af,
                              enum AVSampleFormat sample_fmt,
                              const AVChannelLayout *ch_layout,
                              int nb_samples)
{
    if (!af || !ch_layout || nb_samples <= 0)
        return AVERROR(EINVAL);

    int nb_channels = ch_layout->nb_channels;
    int ret;

    uint8_t **data = NULL;
    ret = av_samples_alloc_array_and_samples(&data, NULL,
                                             nb_channels,
                                             nb_samples,
                                             sample_fmt,
                                             0);
    if (ret < 0)
        return ret;

    ret = av_samples_set_silence(data,
                                 0,
                                 nb_samples,
                                 nb_channels,
                                 sample_fmt);
    if (ret < 0) {
        av_freep(&data[0]);
        av_freep(&data);
        return ret;
    }

    ret = av_audio_fifo_write(af, (void **)data, nb_samples);

    av_freep(&data[0]);
    av_freep(&data);
    return ret;
}
