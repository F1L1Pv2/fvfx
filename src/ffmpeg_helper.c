#include <ffmpeg_helper.h>
#include <libavutil/samplefmt.h>
#include <libavutil/audio_fifo.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>

void mix_audio(uint8_t** base, uint8_t** added,size_t nb_samples, size_t num_channels,enum AVSampleFormat sample_fmt,double volume, double panning)
{
    if (sample_fmt != AV_SAMPLE_FMT_FLTP && sample_fmt != AV_SAMPLE_FMT_FLT) {
        assert(0 && "Unsupported sample format");
        return;
    }

    if (num_channels != 1 && num_channels != 2) {
        assert(0 && "Only mono and stereo supported");
        return;
    }

    // Clamp panning to [-1, 1]
    if (panning < -1.0) panning = -1.0;
    if (panning >  1.0) panning =  1.0;

    // Compute left/right gain based on panning
    // Equal power panning law (keeps perceived loudness constant)
    float left_gain, right_gain;
    if (num_channels == 1) {
        left_gain = right_gain = 1.0f; // irrelevant, mono only
    } else {
        float angle = (float)((panning + 1.0) * M_PI_4); // maps [-1,1] -> [0, π/2]
        left_gain  = cosf(angle);
        right_gain = sinf(angle);
    }

    if (sample_fmt == AV_SAMPLE_FMT_FLTP) {
        // Planar float
        for (size_t ch = 0; ch < num_channels; ch++) {
            float *base_ch  = (float*) base[ch];
            float *added_ch = (float*) added[ch];

            float ch_gain = 1.0f;
            if (num_channels == 2) {
                ch_gain = (ch == 0) ? left_gain : right_gain;
            }

            for (size_t i = 0; i < nb_samples; i++) {
                float mixed = base_ch[i] + (added_ch[i] * (float)volume * ch_gain);

                if (mixed > 1.0f)  mixed = 1.0f;
                if (mixed < -1.0f) mixed = -1.0f;

                base_ch[i] = mixed;
            }
        }

    } else if (sample_fmt == AV_SAMPLE_FMT_FLT) {
        // Interleaved float
        float *base_packed  = (float*) base[0];
        float *added_packed = (float*) added[0];

        for (size_t i = 0; i < nb_samples; i++) {
            if (num_channels == 1) {
                // Mono
                float mixed = base_packed[i] + (added_packed[i] * (float)volume);

                if (mixed > 1.0f)  mixed = 1.0f;
                if (mixed < -1.0f) mixed = -1.0f;

                base_packed[i] = mixed;
            } else {
                // Stereo: L = even index, R = odd index
                size_t li = i * 2;
                size_t ri = li + 1;

                float left_mixed  = base_packed[li] + (added_packed[li] * (float)volume * left_gain);
                float right_mixed = base_packed[ri] + (added_packed[ri] * (float)volume * right_gain);

                if (left_mixed > 1.0f)  left_mixed = 1.0f;
                if (left_mixed < -1.0f) left_mixed = -1.0f;
                if (right_mixed > 1.0f)  right_mixed = 1.0f;
                if (right_mixed < -1.0f) right_mixed = -1.0f;

                base_packed[li] = left_mixed;
                base_packed[ri] = right_mixed;
            }
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
