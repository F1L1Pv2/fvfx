#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "ffmpeg_media.h"

static bool initializeMediaContext(Media* media, const char* filename);
static bool initializeDecoder(Media* media);

bool ffmpegMediaInit(const char* filename, Media* media) 
{
    memset(media, 0, sizeof(Media));
    
    if (!initializeMediaContext(media, filename)) goto error;
    if (!initializeDecoder(media)) goto error;

    // media->duration = (double)media->formatContext->streams[media->videoStreamIndex]->duration * 
    //        av_q2d(media->formatContext->streams[media->videoStreamIndex]->time_base);
    
    // media->videoFrameRate = av_q2d(media->formatContext->streams[media->videoStreamIndex]->avg_frame_rate);
    // if (media->videoFrameRate <= 0.0) {
    //     media->videoFrameRate = av_q2d(media->formatContext->streams[media->videoStreamIndex]->r_frame_rate);
    // }

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

                    int channels = media->audioCodecContext->ch_layout.nb_channels;
                    int nb_samples = media->audioFrame->nb_samples;
                    enum AVSampleFormat src_fmt = media->audioCodecContext->sample_fmt;
                    int bytes_per_sample = av_get_bytes_per_sample(src_fmt);
                    bool is_planar = av_sample_fmt_is_planar(src_fmt);

                    int plane_size = nb_samples * bytes_per_sample;
                    int total_size = channels * plane_size;

                    if (frame->audio.data == NULL || frame->audio.nb_samples < total_size) {
                        free(frame->audio.data);
                        frame->audio.data = calloc(total_size, 1);
                        frame->audio.nb_samples = total_size;
                    }

                    if (is_planar) {
                        for (int ch = 0; ch < channels; ch++) {
                            memcpy(
                                (uint8_t*)frame->audio.data + ch * plane_size,
                                media->audioFrame->data[ch],
                                plane_size
                            );
                        }
                    } else {
                        const uint8_t *src = media->audioFrame->data[0];
                        for (int s = 0; s < nb_samples; s++) {
                            for (int ch = 0; ch < channels; ch++) {
                                memcpy(
                                    (uint8_t*)frame->audio.data + ch * plane_size + s * bytes_per_sample,
                                    src + (s * channels + ch) * bytes_per_sample,
                                    bytes_per_sample
                                );
                            }
                        }
                    }

                    frame->audio.channels = channels;
                    frame->audio.sampleRate = media->audioCodecContext->sample_rate;
                    frame->audio.nb_samples = nb_samples;

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
    
            if(frame->video.width == 0 && frame->video.height == 0 && frame->video.data == NULL){
                media->swsContext = sws_getContext(
                    media->videoFrame->width, media->videoFrame->height, media->videoCodecContext->pix_fmt,
                    media->videoFrame->width, media->videoFrame->height, AV_PIX_FMT_RGBA,
                    SWS_FAST_BILINEAR, NULL, NULL, NULL
                );
                frame->video.width = media->videoFrame->width;
                frame->video.height = media->videoFrame->height;
                frame->video.data = malloc(frame->video.width*frame->video.height*sizeof(uint32_t));
            }
    
            // Convert frame to RGB
            uint8_t* dest[4] = {(uint8_t*)frame->video.data, NULL, NULL, NULL};
            int dest_linesize[4] = {media->videoFrame->width * sizeof(uint32_t), 0, 0, 0};
            sws_scale(media->swsContext, 
                    (const uint8_t* const*)media->videoFrame->data, 
                    media->videoFrame->linesize, 
                    0, 
                    media->videoFrame->height, 
                    dest, 
                    dest_linesize);
    
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
 
    do {
        if (!ffmpegMediaGetFrame(media, frame)) break;
    } while(frame->frameTime < time_seconds);

    return true;
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

static bool initializeDecoder(Media* media) {
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

    if(media->audioStreamIndex != -1) media->audioFrame = av_frame_alloc();
    
    media->videoFrame = av_frame_alloc();
    media->packet = av_packet_alloc();
    return media->videoFrame && media->packet;
}

double ffmpegMediaDuration(Media* media){
    return (double)media->formatContext->streams[media->videoStreamIndex]->duration * 
            av_q2d(media->formatContext->streams[media->videoStreamIndex]->time_base);
}