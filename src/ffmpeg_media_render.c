#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "ffmpeg_media_render.h"

bool ffmpegMediaRenderInit(const char* filename, size_t width, size_t height, double fps, size_t sampleRate, bool stereo, bool hasAudio, MediaRenderContext* render){
    memset(render, 0, sizeof(MediaRenderContext));

    avformat_alloc_output_context2(&render->formatContext, NULL, NULL, filename);
    if (!render->formatContext) return false;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) return false;

    render->videoStream = avformat_new_stream(render->formatContext, NULL);
    if (!render->videoStream) return false;

    render->videoCodecContext = avcodec_alloc_context3(codec);
    if (!render->videoCodecContext) return false;

    render->videoCodecContext->codec_id = AV_CODEC_ID_H264;
    render->videoCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    render->videoCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    render->videoCodecContext->width = width;
    render->videoCodecContext->height = height;

    render->videoCodecContext->framerate = (AVRational){fps,1};
    render->videoStream->avg_frame_rate = render->videoCodecContext->framerate;
    render->videoCodecContext->time_base = (AVRational){1,(int)fps};
    render->videoStream->time_base = render->videoCodecContext->time_base;

    if (render->formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
        render->videoCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(render->videoCodecContext, codec, NULL) < 0) return false;
    if (avcodec_parameters_from_context(render->videoStream->codecpar, render->videoCodecContext) < 0) return false;

    render->packet = av_packet_alloc();
    render->videoFrame = av_frame_alloc();
    render->videoFrame->format = render->videoCodecContext->pix_fmt;
    render->videoFrame->width = render->videoCodecContext->width;
    render->videoFrame->height = render->videoCodecContext->height;
    av_frame_get_buffer(render->videoFrame, 32);

    render->swsContext = sws_getContext(
        render->videoCodecContext->width, render->videoCodecContext->height, AV_PIX_FMT_RGBA,
        render->videoCodecContext->width, render->videoCodecContext->height, render->videoCodecContext->pix_fmt,
        SWS_BICUBIC, NULL, NULL, NULL
    );

    if (hasAudio) {
        const AVCodec* audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!audioCodec) return false;

        render->audioStream = avformat_new_stream(render->formatContext, NULL);
        if (!render->audioStream) return false;

        render->audioCodecContext = avcodec_alloc_context3(audioCodec);
        if (!render->audioCodecContext) return false;

        render->audioCodecContext->sample_rate = sampleRate;
        av_channel_layout_default(&render->audioCodecContext->ch_layout, stereo ? 2 : 1);
        render->audioCodecContext->sample_fmt = audioCodec->sample_fmts[0];
        render->audioCodecContext->time_base = (AVRational){1, (int)sampleRate};

        render->audioStream->time_base = render->audioCodecContext->time_base;

        if (render->formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
            render->audioCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(render->audioCodecContext, audioCodec, NULL) < 0) return false;
        if (avcodec_parameters_from_context(render->audioStream->codecpar, render->audioCodecContext) < 0) return false;

        render->audioFrame = av_frame_alloc();
        render->audioFrame->format = render->audioCodecContext->sample_fmt;
        render->audioFrame->ch_layout = render->audioCodecContext->ch_layout;
        render->audioFrame->sample_rate = render->audioCodecContext->sample_rate;
        render->audioFrame->nb_samples = render->audioCodecContext->frame_size;

        av_frame_get_buffer(render->audioFrame, 0);
        render->audioPacket = av_packet_alloc();

        if(render->audioCodecContext->ch_layout.nb_channels > 2){
            fprintf(stderr, "more than 2 audio channels is not supported\n");
            return false;
        }

        render->audioFifo = av_audio_fifo_alloc(
            render->audioCodecContext->sample_fmt,
            render->audioCodecContext->ch_layout.nb_channels,
            1
        );
        if (!render->audioFifo) {
            fprintf(stderr, "Couldn't create audio FIFO\n");
            return false;
        }
    }

    if (!(render->formatContext->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&render->formatContext->pb, filename, AVIO_FLAG_WRITE) < 0) return false;
    }

    if (avformat_write_header(render->formatContext, NULL) < 0) return false;

    return true;
}

bool ffmpegMediaRenderPassFrame(MediaRenderContext* render, const RenderFrame* frame) {
    if (frame->type == RENDER_FRAME_TYPE_AUDIO) {
        if (av_audio_fifo_write(render->audioFifo, (void**)frame->data, frame->size) < frame->size) {
            fprintf(stderr, "Couldn't write to FIFO\n");
            return false;
        }

        while (av_audio_fifo_size(render->audioFifo) >= render->audioCodecContext->frame_size) {
            av_frame_make_writable(render->audioFrame);
            int read = av_audio_fifo_read(render->audioFifo, (void**)render->audioFrame->data, render->audioCodecContext->frame_size);
            assert(read == render->audioCodecContext->frame_size);

            render->audioFrame->pts = render->audioFrameCount;
            render->audioFrameCount += read;

            if (avcodec_send_frame(render->audioCodecContext, render->audioFrame) < 0)
                return false;

            while (avcodec_receive_packet(render->audioCodecContext, render->audioPacket) == 0) {
                render->audioPacket->stream_index = render->audioStream->index;
                av_packet_rescale_ts(render->audioPacket,
                                    render->audioCodecContext->time_base,
                                    render->audioStream->time_base);
                av_interleaved_write_frame(render->formatContext, render->audioPacket);
                av_packet_unref(render->audioPacket);
            }
        }

        return true;
    }

    if(frame->type == RENDER_FRAME_TYPE_VIDEO){
        int width = render->videoCodecContext->width;
        int height = render->videoCodecContext->height;
    
        const uint8_t* srcSlice[4] = {(uint8_t*)frame->data, NULL, NULL, NULL};
        int srcStride[4] = { (int)(width * sizeof(uint32_t)), 0, 0, 0 };

        av_frame_make_writable(render->videoFrame);
        sws_scale(render->swsContext, srcSlice, srcStride, 0, height, render->videoFrame->data, render->videoFrame->linesize);

        render->videoFrame->pts = av_rescale_q(render->videoFrameCount++,
                                       av_inv_q(render->videoCodecContext->framerate),
                                       render->videoCodecContext->time_base);
    
        if (avcodec_send_frame(render->videoCodecContext, render->videoFrame) < 0) return false;
    
        while (avcodec_receive_packet(render->videoCodecContext, render->packet) == 0) {
            render->packet->stream_index = render->videoStream->index;
            av_packet_rescale_ts(render->packet, render->videoCodecContext->time_base, render->videoStream->time_base);
            av_interleaved_write_frame(render->formatContext, render->packet);
            av_packet_unref(render->packet);
        }
        return true;
    }

    fprintf(stderr, "Unsupported frame type!\n");
    abort();
}

void ffmpegMediaRenderFinish(MediaRenderContext* render) {
    int ret;

    avcodec_send_frame(render->videoCodecContext, NULL);
    while ((ret = avcodec_receive_packet(render->videoCodecContext, render->packet)) == 0) {
        render->packet->stream_index = render->videoStream->index;
        av_packet_rescale_ts(render->packet,
                             render->videoCodecContext->time_base,
                             render->videoStream->time_base);
        av_interleaved_write_frame(render->formatContext, render->packet);
        av_packet_unref(render->packet);
    }

    if (render->audioCodecContext) {
        while (av_audio_fifo_size(render->audioFifo) > 0) {
            av_frame_make_writable(render->audioFrame);

            int nb_samples = FFMIN(av_audio_fifo_size(render->audioFifo),
                                   render->audioCodecContext->frame_size);

            render->audioFrame->nb_samples = nb_samples;

            int read = av_audio_fifo_read(render->audioFifo,
                                          (void**)render->audioFrame->data,
                                          nb_samples);
            if (read <= 0) {
                break;
            }
            assert(read == nb_samples);

            render->audioFrame->pts = render->audioFrameCount;
            render->audioFrameCount += read;

            ret = avcodec_send_frame(render->audioCodecContext, render->audioFrame);
            if (ret < 0) {
                break;
            }

            while ((ret = avcodec_receive_packet(render->audioCodecContext, render->audioPacket)) == 0) {
                render->audioPacket->stream_index = render->audioStream->index;
                av_packet_rescale_ts(render->audioPacket,
                                     render->audioCodecContext->time_base,
                                     render->audioStream->time_base);
                av_interleaved_write_frame(render->formatContext, render->audioPacket);
                av_packet_unref(render->audioPacket);
            }
        }

        avcodec_send_frame(render->audioCodecContext, NULL);
        while ((ret = avcodec_receive_packet(render->audioCodecContext, render->audioPacket)) == 0) {
            render->audioPacket->stream_index = render->audioStream->index;
            av_packet_rescale_ts(render->audioPacket,
                                 render->audioCodecContext->time_base,
                                 render->audioStream->time_base);
            av_interleaved_write_frame(render->formatContext, render->audioPacket);
            av_packet_unref(render->audioPacket);
        }

        av_audio_fifo_free(render->audioFifo);
    }

    av_write_trailer(render->formatContext);

    if (!(render->formatContext->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&render->formatContext->pb);
    }

    avcodec_free_context(&render->videoCodecContext);
    av_frame_free(&render->videoFrame);
    av_packet_free(&render->packet);
    sws_freeContext(render->swsContext);
    avformat_free_context(render->formatContext);

    if (render->audioCodecContext) avcodec_free_context(&render->audioCodecContext);
    if (render->audioFrame) av_frame_free(&render->audioFrame);
    if (render->audioPacket) av_packet_free(&render->audioPacket);

    memset(render, 0, sizeof(MediaRenderContext));
}
