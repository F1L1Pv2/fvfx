#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "ffmpeg_media.h"

static bool initializeMediaContext(Media* media, const char* filename);
static bool initializeDecoder(Media* media, size_t desiredSampleRate, bool desiredStereo);

bool ffmpegMediaInit(const char* filename, size_t desiredSampleRate, bool desiredStereo, Media* media) 
{
    memset(media, 0, sizeof(Media));
    
    if (!initializeMediaContext(media, filename)) goto error;
    if (!initializeDecoder(media, desiredSampleRate, desiredStereo)) goto error;

    return true;

error:
    ffmpegMediaUninit(media);
    return false;
}

bool ffmpegMediaGetFrame(Media* media, Frame* frame) {
    av_frame_unref(media->videoFrame);
    av_packet_unref(media->packet);
    int response;
    while (av_read_frame(media->formatContext, media->packet) >= 0) {
        if (media->packet->stream_index == media->audioStreamIndex) {
            response = avcodec_send_packet(media->audioCodecContext, media->packet);
            if (response >= 0) {
                response = avcodec_receive_frame(media->audioCodecContext, media->audioFrame);
                if (response >= 0) {
                    frame->type = FRAME_TYPE_AUDIO;
                    frame->frameTime = (double)media->audioFrame->pts * 
                        av_q2d(media->formatContext->streams[media->audioStreamIndex]->time_base);

                    media->tempFrame.audio.nb_samples = swr_convert(media->swrContext, &media->tempFrame.audio.data, media->tempFrame.audio.count, (const uint8_t* const *)media->audioFrame->data, media->audioFrame->nb_samples);
                    frame->audio = media->tempFrame.audio;
                    return true;
                }
            }
            av_packet_unref(media->packet);
            continue;
        }

        if (media->packet->stream_index == media->videoStreamIndex) {
            response = avcodec_send_packet(media->videoCodecContext, media->packet);
            if (response < 0) {
                av_packet_unref(media->packet);
                continue;
            }
    
            response = avcodec_receive_frame(media->videoCodecContext, media->videoFrame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                av_packet_unref(media->packet);
                continue;
            } 
            else if (response < 0) {
                av_packet_unref(media->packet);
                return false;
            }
    
            frame->type = FRAME_TYPE_VIDEO;
    
            // Convert frame to RGB
            uint8_t* dest[4] = {(uint8_t*)media->tempFrame.video.data, NULL, NULL, NULL};
            int dest_linesize[4] = {media->videoFrame->width * sizeof(uint32_t), 0, 0, 0};
            sws_scale(media->swsContext, 
                    (const uint8_t* const*)media->videoFrame->data, 
                    media->videoFrame->linesize, 
                    0, 
                    media->videoFrame->height, 
                    dest, 
                    dest_linesize);

            frame->video = media->tempFrame.video;
    
            frame->frameTime = (double)media->videoFrame->pts * 
                av_q2d(media->formatContext->streams[media->videoStreamIndex]->time_base);
                
            return true;
        }

    }

    return false;
}

bool ffmpegMediaSeek(Media* media, Frame* frame, double time_seconds) {
    int64_t target_pts = time_seconds / av_q2d(media->formatContext->streams[media->videoStreamIndex]->time_base);

    avcodec_flush_buffers(media->videoCodecContext);
    avcodec_flush_buffers(media->audioCodecContext);

    int ret = av_seek_frame(media->formatContext, media->videoStreamIndex, target_pts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        printf("Seek failed: %s\n", av_err2str(ret));
        return false;
    }

    avcodec_flush_buffers(media->videoCodecContext);
    avcodec_flush_buffers(media->audioCodecContext);

    av_frame_unref(media->videoFrame);
    av_packet_unref(media->packet);

    while (av_read_frame(media->formatContext, media->packet) >= 0) {
        if (media->packet->stream_index == media->videoStreamIndex) {
            ret = avcodec_send_packet(media->videoCodecContext, media->packet);
            av_packet_unref(media->packet);
            if (ret < 0) continue;

            ret = avcodec_receive_frame(media->videoCodecContext, media->videoFrame);
            if (ret == 0) {
                double pts_time = (double)media->videoFrame->pts *
                    av_q2d(media->formatContext->streams[media->videoStreamIndex]->time_base);

                if (pts_time >= time_seconds) return true;
            }
        } else {
            av_packet_unref(media->packet);
        }
    }

    return false;
}

void ffmpegMediaUninit(Media* media) {
    if (media->swsContext) sws_freeContext(media->swsContext);
    if (media->videoCodecContext) avcodec_free_context(&media->videoCodecContext);
    if (media->videoFrame) av_frame_free(&media->videoFrame);
    if (media->packet) av_packet_free(&media->packet);
    if (media->audioFrame) av_frame_free(&media->audioFrame);
    if (media->audioCodecContext) avcodec_free_context(&media->audioCodecContext);
    if (media->formatContext) {
        avformat_close_input(&media->formatContext);
        avformat_free_context(media->formatContext);
    }
    if(media->swrContext) swr_free(&media->swrContext);
    
    memset(media, 0, sizeof(Media));
}

static bool initializeMediaContext(Media* media, const char* filename) {
    media->formatContext = avformat_alloc_context();
    if (!media->formatContext) return false;
    
    if (avformat_open_input(&media->formatContext, filename, NULL, NULL) < 0) {
        avformat_free_context(media->formatContext);
        return false;
    }
    
    if (avformat_find_stream_info(media->formatContext, NULL) < 0) return false;
    
    return true;
}

static bool initializeDecoder(Media* media, size_t desiredSampleRate, bool desiredStereo) {
    media->videoStreamIndex = -1;
    for (int i = 0; i < media->formatContext->nb_streams; i++) {
        AVStream* stream = media->formatContext->streams[i];
        if (media->videoStreamIndex == -1 && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            media->videoStreamIndex = i;
            AVCodecParameters* codecParameters = stream->codecpar;

            const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);
            if (!codec) return false;
            
            media->videoCodecContext = avcodec_alloc_context3(codec);
            if (!media->videoCodecContext) return false;
            
            if (avcodec_parameters_to_context(media->videoCodecContext, codecParameters) < 0) return false;
            
            if (avcodec_open2(media->videoCodecContext, codec, NULL) < 0) return false;

            break;
        }
    }
    
    if (media->videoStreamIndex == -1) return false;

    media->audioStreamIndex = -1;
    for (int i = 0; i < media->formatContext->nb_streams; i++) {
        AVStream* stream = media->formatContext->streams[i];
        if (media->audioStreamIndex == -1 && stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            media->audioStreamIndex = i;
            AVCodecParameters* codecParameters = stream->codecpar;

            const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);
            if (!codec) return false;

            media->audioCodecContext = avcodec_alloc_context3(codec);
            if (!media->audioCodecContext) return false;

            if (avcodec_parameters_to_context(media->audioCodecContext, codecParameters) < 0) return false;

            if (avcodec_open2(media->audioCodecContext, codec, NULL) < 0) return false;

            if(media->audioCodecContext->ch_layout.nb_channels > 2){
                fprintf(stderr, "more than 2 audio channels is not supported\n");
                return false;
            }
            break;
        }
    }

    if(media->audioStreamIndex != -1) {
        media->audioFrame = av_frame_alloc();

        media->swrContext = swr_alloc();
        if(!media->swrContext){
            fprintf(stderr, "Couldn't allocate resampler\n");
            return false;
        }

        if(swr_alloc_set_opts2(&media->swrContext,
            desiredStereo ? &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO
                          : &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO,
            AV_SAMPLE_FMT_FLT,
            desiredSampleRate,
            &media->audioCodecContext->ch_layout,
            media->audioCodecContext->sample_fmt,
            media->audioCodecContext->sample_rate,
            0,
            NULL
        ) < 0) {
            fprintf(stderr, "Couldn't set ops for resampler\n");
            return false;
        }

        if(swr_init(media->swrContext) < 0) {
            fprintf(stderr, "Couldn't initialize resampler\n");
            return false;
        }

        media->tempFrame.audio.nb_samples = 0;
        media->tempFrame.audio.count = desiredSampleRate/4;
        if(av_samples_alloc(&media->tempFrame.audio.data,&media->tempFrame.audio.capacity,desiredStereo ? 2 : 1, media->tempFrame.audio.count, AV_SAMPLE_FMT_FLT, 1) < 0){
            fprintf(stderr, "Couldn't alloc space for audio sample\n");
            return false;
        }
        memset(media->tempFrame.audio.data, 0, media->tempFrame.audio.capacity);
    }
    
    media->videoFrame = av_frame_alloc();
    media->packet = av_packet_alloc();
    media->swsContext = sws_getContext(
        media->videoCodecContext->width, media->videoCodecContext->height, media->videoCodecContext->pix_fmt,
        media->videoCodecContext->width, media->videoCodecContext->height, AV_PIX_FMT_RGBA,
        SWS_FAST_BILINEAR, NULL, NULL, NULL
    );
    media->tempFrame.video.width = media->videoCodecContext->width;
    media->tempFrame.video.height = media->videoCodecContext->height;
    media->tempFrame.video.data = malloc(media->tempFrame.video.width*media->tempFrame.video.height*sizeof(uint32_t));
    return media->videoFrame && media->packet;
}

double ffmpegMediaDuration(Media* media){
    return (double)media->formatContext->streams[media->videoStreamIndex]->duration * 
            av_q2d(media->formatContext->streams[media->videoStreamIndex]->time_base);
}