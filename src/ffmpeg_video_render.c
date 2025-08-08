#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "ffmpeg_video_render.h"

bool ffmpegVideoRenderInit(const Video* sourceVideo, const char* filename, VideoRenderContext* render) {
    memset(render, 0, sizeof(VideoRenderContext));

    render->width = sourceVideo->codecContext->width;
    render->height = sourceVideo->codecContext->height;
    render->sourceVideo = sourceVideo;
    AVStream* srcStream = sourceVideo->formatContext->streams[sourceVideo->videoStreamIndex];
    render->frameRate = av_q2d(srcStream->avg_frame_rate);

    avformat_alloc_output_context2(&render->formatContext, NULL, NULL, filename);
    if (!render->formatContext) return false;

    const AVCodec* codec = avcodec_find_encoder(sourceVideo->codecContext->codec_id);
    if (!codec) return false;

    render->stream = avformat_new_stream(render->formatContext, NULL);
    if (!render->stream) return false;

    render->codecContext = avcodec_alloc_context3(codec);
    if (!render->codecContext) return false;

    render->codecContext->codec_id = sourceVideo->codecContext->codec_id;
    render->codecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    render->codecContext->pix_fmt = sourceVideo->codecContext->pix_fmt;
    render->codecContext->width = render->width;
    render->codecContext->height = render->height;

    render->codecContext->framerate = srcStream->avg_frame_rate; 
    render->stream->avg_frame_rate = srcStream->avg_frame_rate;
    render->codecContext->time_base = srcStream->time_base;
    render->stream->time_base = srcStream->time_base;

    render->codecContext->bit_rate = sourceVideo->codecContext->bit_rate;
    render->codecContext->gop_size = sourceVideo->codecContext->gop_size;
    render->codecContext->max_b_frames = sourceVideo->codecContext->max_b_frames;
    render->codecContext->profile = sourceVideo->codecContext->profile;
    render->codecContext->level = sourceVideo->codecContext->level;
    render->codecContext->colorspace = sourceVideo->codecContext->colorspace;
    render->codecContext->color_range = sourceVideo->codecContext->color_range;

    if (render->formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
        render->codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(render->codecContext, codec, NULL) < 0) return false;
    if (avcodec_parameters_from_context(render->stream->codecpar, render->codecContext) < 0) return false;

    render->packet = av_packet_alloc();
    render->frame = av_frame_alloc();
    render->frame->format = render->codecContext->pix_fmt;
    render->frame->width = render->width;
    render->frame->height = render->height;
    av_frame_get_buffer(render->frame, 32);

    render->swsContext = sws_getContext(
        render->width, render->height, AV_PIX_FMT_RGBA,
        render->width, render->height, render->codecContext->pix_fmt,
        SWS_BICUBIC, NULL, NULL, NULL
    );

    if (!(render->formatContext->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&render->formatContext->pb, filename, AVIO_FLAG_WRITE) < 0) return false;
    }

    if (avformat_write_header(render->formatContext, NULL) < 0) return false;

    return true;
}

bool ffmpegVideoRenderPassFrame(VideoRenderContext* render, const Frame* frame) {
    if(frame->type != FRAME_TYPE_VIDEO){
        fprintf(stderr, "Unsupported frame type!\n");
        abort();
    }

    const uint8_t* srcSlice[4] = {(uint8_t*)frame->as.video.data, NULL, NULL, NULL};
    int srcStride[4] = { (int)(render->width * sizeof(uint32_t)), 0, 0, 0 };

    av_frame_make_writable(render->frame);
    sws_scale(render->swsContext, srcSlice, srcStride, 0, render->height, render->frame->data, render->frame->linesize);

    render->frame->pts = frame->frameTime * (double)render->codecContext->time_base.den / (double)render->codecContext->time_base.num;

    if (avcodec_send_frame(render->codecContext, render->frame) < 0) return false;

    while (avcodec_receive_packet(render->codecContext, render->packet) == 0) {
        render->packet->stream_index = render->stream->index;
        av_packet_rescale_ts(render->packet, render->codecContext->time_base, render->stream->time_base);
        av_interleaved_write_frame(render->formatContext, render->packet);
        av_packet_unref(render->packet);
    }

    return true;
}

void ffmpegVideoRenderFinish(VideoRenderContext* render) {
    avcodec_send_frame(render->codecContext, NULL);
    while (avcodec_receive_packet(render->codecContext, render->packet) == 0) {
        render->packet->stream_index = render->stream->index;
        av_packet_rescale_ts(render->packet, render->codecContext->time_base, render->stream->time_base);
        av_interleaved_write_frame(render->formatContext, render->packet);
        av_packet_unref(render->packet);
    }

    av_write_trailer(render->formatContext);

    if (!(render->formatContext->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&render->formatContext->pb);
    }

    avcodec_free_context(&render->codecContext);
    av_frame_free(&render->frame);
    av_packet_free(&render->packet);
    sws_freeContext(render->swsContext);
    avformat_free_context(render->formatContext);
    
    memset(render, 0, sizeof(VideoRenderContext));
}