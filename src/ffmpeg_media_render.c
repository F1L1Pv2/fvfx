#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "ffmpeg_media_render.h"

bool ffmpegMediaRenderInit(const Media* sourceVideo, const char* filename, MediaRenderContext* render) {
    memset(render, 0, sizeof(MediaRenderContext));

    AVStream* videoStream = sourceVideo->formatContext->streams[sourceVideo->videoStreamIndex];

    avformat_alloc_output_context2(&render->formatContext, NULL, NULL, filename);
    if (!render->formatContext) return false;

    const AVCodec* codec = avcodec_find_encoder(sourceVideo->videoCodecContext->codec_id);
    if (!codec) return false;

    render->videoStream = avformat_new_stream(render->formatContext, NULL);
    if (!render->videoStream) return false;

    render->videoCodecContext = avcodec_alloc_context3(codec);
    if (!render->videoCodecContext) return false;

    render->videoCodecContext->codec_id = sourceVideo->videoCodecContext->codec_id;
    render->videoCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    render->videoCodecContext->pix_fmt = sourceVideo->videoCodecContext->pix_fmt;
    render->videoCodecContext->width = sourceVideo->videoCodecContext->width;
    render->videoCodecContext->height = sourceVideo->videoCodecContext->height;

    render->videoCodecContext->framerate = videoStream->avg_frame_rate; 
    render->videoStream->avg_frame_rate = videoStream->avg_frame_rate;
    render->videoCodecContext->time_base = videoStream->time_base;
    render->videoStream->time_base = videoStream->time_base;

    render->videoCodecContext->bit_rate = sourceVideo->videoCodecContext->bit_rate;
    render->videoCodecContext->gop_size = sourceVideo->videoCodecContext->gop_size;
    render->videoCodecContext->max_b_frames = sourceVideo->videoCodecContext->max_b_frames;
    render->videoCodecContext->profile = sourceVideo->videoCodecContext->profile;
    render->videoCodecContext->level = sourceVideo->videoCodecContext->level;
    render->videoCodecContext->colorspace = sourceVideo->videoCodecContext->colorspace;
    render->videoCodecContext->color_range = sourceVideo->videoCodecContext->color_range;

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

    if (sourceVideo->audioCodecContext) {
        const AVCodec* audioCodec = avcodec_find_encoder(sourceVideo->audioCodecContext->codec_id);
        if (!audioCodec) return false;

        render->audioStream = avformat_new_stream(render->formatContext, NULL);
        if (!render->audioStream) return false;

        render->audioCodecContext = avcodec_alloc_context3(audioCodec);
        if (!render->audioCodecContext) return false;

        render->audioCodecContext->sample_rate = sourceVideo->audioCodecContext->sample_rate;
        render->audioCodecContext->ch_layout = sourceVideo->audioCodecContext->ch_layout;
        render->audioCodecContext->sample_fmt = audioCodec->sample_fmts[0];
        render->audioCodecContext->bit_rate = sourceVideo->audioCodecContext->bit_rate;
        render->audioCodecContext->time_base = (AVRational){1, sourceVideo->audioCodecContext->sample_rate};

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
    }

    if (!(render->formatContext->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&render->formatContext->pb, filename, AVIO_FLAG_WRITE) < 0) return false;
    }

    if (avformat_write_header(render->formatContext, NULL) < 0) return false;

    return true;
}

bool ffmpegMediaRenderPassFrame(MediaRenderContext* render, const Frame* frame) {
    if(frame->type == FRAME_TYPE_AUDIO){
        av_frame_make_writable(render->audioFrame);
        int data_size = av_samples_get_buffer_size(NULL, 
                                                render->audioCodecContext->ch_layout.nb_channels,
                                                render->audioFrame->nb_samples,
                                                render->audioCodecContext->sample_fmt, 1);
        memcpy(render->audioFrame->data[0], frame->audio.data, data_size);

        render->audioFrame->pts = frame->frameTime * render->audioCodecContext->sample_rate;

        if (avcodec_send_frame(render->audioCodecContext, render->audioFrame) < 0) return false;

        while (avcodec_receive_packet(render->audioCodecContext, render->audioPacket) == 0) {
            render->audioPacket->stream_index = render->audioStream->index;
            av_packet_rescale_ts(render->audioPacket,
                                render->audioCodecContext->time_base,
                                render->audioStream->time_base);
            av_interleaved_write_frame(render->formatContext, render->audioPacket);
            av_packet_unref(render->audioPacket);
        }

        return true;
    }

    if(frame->type == FRAME_TYPE_VIDEO){
        int width = render->videoCodecContext->width;
        int height = render->videoCodecContext->height;
    
        const uint8_t* srcSlice[4] = {(uint8_t*)frame->video.data, NULL, NULL, NULL};
        int srcStride[4] = { (int)(width * sizeof(uint32_t)), 0, 0, 0 };

        av_frame_make_writable(render->videoFrame);
        sws_scale(render->swsContext, srcSlice, srcStride, 0, height, render->videoFrame->data, render->videoFrame->linesize);
    
        render->videoFrame->pts = frame->frameTime * (double)render->videoCodecContext->time_base.den / (double)render->videoCodecContext->time_base.num;
    
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
    avcodec_send_frame(render->videoCodecContext, NULL);
    while (avcodec_receive_packet(render->videoCodecContext, render->packet) == 0) {
        render->packet->stream_index = render->videoStream->index;
        av_packet_rescale_ts(render->packet, render->videoCodecContext->time_base, render->videoStream->time_base);
        av_interleaved_write_frame(render->formatContext, render->packet);
        av_packet_unref(render->packet);
    }

    av_write_trailer(render->formatContext);

    if (!(render->formatContext->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&render->formatContext->pb);
    }

    if (render->audioCodecContext) {
        avcodec_send_frame(render->audioCodecContext, NULL);
        while (avcodec_receive_packet(render->audioCodecContext, render->audioPacket) == 0) {
            render->audioPacket->stream_index = render->audioStream->index;
            av_packet_rescale_ts(render->audioPacket,
                                render->audioCodecContext->time_base,
                                render->audioStream->time_base);
            av_interleaved_write_frame(render->formatContext, render->audioPacket);
            av_packet_unref(render->audioPacket);
        }
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