#include <ffmpeg_helper.h>
#include <libavutil/samplefmt.h>
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
