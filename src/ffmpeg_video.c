#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "ffmpeg_video.h"

static bool initializeVideoContext(Video* video, const char* filename);
static bool initializeDecoder(Video* video);

bool ffmpegVideoInit(const char* filename, Video* video) 
{
    memset(video, 0, sizeof(Video));
    
    if (!initializeVideoContext(video, filename)) goto error;
    if (!initializeDecoder(video)) goto error;

    video->duration = (double)video->formatContext->streams[video->videoStreamIndex]->duration * 
           av_q2d(video->formatContext->streams[video->videoStreamIndex]->time_base);
    
    video->frameRate = av_q2d(video->formatContext->streams[video->videoStreamIndex]->avg_frame_rate);
    if (video->frameRate <= 0.0) {
        video->frameRate = av_q2d(video->formatContext->streams[video->videoStreamIndex]->r_frame_rate);
    }

    return true;

error:
    ffmpegVideoUninit(video);
    return false;
}

bool ffmpegVideoGetFrame(Video* video, Frame* frame) {
    av_frame_unref(video->frame);
    av_packet_unref(video->packet);
    int response;
    while (av_read_frame(video->formatContext, video->packet) >= 0) {
        if (video->packet->stream_index == video->audioStreamIndex) {
            response = avcodec_send_packet(video->audioCodecContext, video->packet);
            if (response >= 0) {
                response = avcodec_receive_frame(video->audioCodecContext, video->audioFrame);
                if (response >= 0) {
                    frame->type = FRAME_TYPE_AUDIO;
                    frame->frameTime = (double)video->audioFrame->pts * 
                        av_q2d(video->formatContext->streams[video->audioStreamIndex]->time_base);

                    int data_size = av_samples_get_buffer_size(NULL, 
                                            video->audioCodecContext->ch_layout.nb_channels,
                                            video->audioFrame->nb_samples,
                                            video->audioCodecContext->sample_fmt, 1);

                    // Only (re)allocate if needed
                    if (frame->audio.data == NULL || frame->audio.size < data_size) {
                        free(frame->audio.data); // Free existing buffer if any
                        frame->audio.data = calloc(data_size, 1);
                        frame->audio.size = data_size; // Store allocated buffer size
                    }

                    memcpy(frame->audio.data, video->audioFrame->data[0], data_size);
                    frame->audio.channels = video->audioCodecContext->ch_layout.nb_channels;
                    frame->audio.sampleRate = video->audioCodecContext->sample_rate;
                    frame->audio.size = data_size; // Set actual data size used

                    return true;
                }
            }
            av_packet_unref(video->packet);
            continue;
        }

        if (video->packet->stream_index == video->videoStreamIndex) {
            response = avcodec_send_packet(video->codecContext, video->packet);
            if (response < 0) {
                av_packet_unref(video->packet);
                continue;
            }
    
            response = avcodec_receive_frame(video->codecContext, video->frame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                av_packet_unref(video->packet);
                continue;
            } 
            else if (response < 0) {
                av_packet_unref(video->packet);
                return false;
            }
    
            frame->type = FRAME_TYPE_VIDEO;
    
            if(frame->video.width == 0 && frame->video.height == 0 && frame->video.data == NULL){
                video->swsContext = sws_getContext(
                    video->frame->width, video->frame->height, video->codecContext->pix_fmt,
                    video->frame->width, video->frame->height, AV_PIX_FMT_RGBA,
                    SWS_FAST_BILINEAR, NULL, NULL, NULL
                );
                frame->video.width = video->frame->width;
                frame->video.height = video->frame->height;
                frame->video.data = malloc(frame->video.width*frame->video.height*sizeof(uint32_t));
            }
    
            // Convert frame to RGB
            uint8_t* dest[4] = {(uint8_t*)frame->video.data, NULL, NULL, NULL};
            int dest_linesize[4] = {video->frame->width * sizeof(uint32_t), 0, 0, 0};
            sws_scale(video->swsContext, 
                    (const uint8_t* const*)video->frame->data, 
                    video->frame->linesize, 
                    0, 
                    video->frame->height, 
                    dest, 
                    dest_linesize);
    
            frame->frameTime = (double)video->frame->pts * 
                av_q2d(video->formatContext->streams[video->videoStreamIndex]->time_base);
                
            return true;
        }

    }

    return false;
}

bool ffmpegVideoSeek(Video* video, Frame* frame, double time_seconds) {
    int64_t target_pts = time_seconds / av_q2d(video->formatContext->streams[video->videoStreamIndex]->time_base);

    int ret = av_seek_frame(video->formatContext, video->videoStreamIndex, target_pts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        printf("Seek failed: %s\n", av_err2str(ret));
        return false;
    }

    avcodec_flush_buffers(video->codecContext);
 
    do {
        if (!ffmpegVideoGetFrame(video, frame)) break;
    } while(frame->frameTime < time_seconds);

    return true;
}

void ffmpegVideoUninit(Video* video) {
    if (video->swsContext) sws_freeContext(video->swsContext);
    if (video->codecContext) avcodec_free_context(&video->codecContext);
    if (video->frame) av_frame_free(&video->frame);
    if (video->packet) av_packet_free(&video->packet);
    if (video->audioFrame) av_frame_free(&video->audioFrame);
    if (video->audioCodecContext) avcodec_free_context(&video->audioCodecContext);
    if (video->formatContext) {
        avformat_close_input(&video->formatContext);
        avformat_free_context(video->formatContext);
    }
    
    memset(video, 0, sizeof(Video));
}

static bool initializeVideoContext(Video* video, const char* filename) {
    video->formatContext = avformat_alloc_context();
    if (!video->formatContext) return false;
    
    if (avformat_open_input(&video->formatContext, filename, NULL, NULL) < 0) {
        avformat_free_context(video->formatContext);
        return false;
    }
    
    if (avformat_find_stream_info(video->formatContext, NULL) < 0) return false;
    
    return true;
}

static bool initializeDecoder(Video* video) {
    video->videoStreamIndex = -1;
    for (int i = 0; i < video->formatContext->nb_streams; i++) {
        AVStream* stream = video->formatContext->streams[i];
        if (video->videoStreamIndex == -1 && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video->videoStreamIndex = i;
            AVCodecParameters* codecParameters = stream->codecpar;

            const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);
            if (!codec) return false;
            
            video->codecContext = avcodec_alloc_context3(codec);
            if (!video->codecContext) return false;
            
            if (avcodec_parameters_to_context(video->codecContext, codecParameters) < 0) return false;
            
            if (avcodec_open2(video->codecContext, codec, NULL) < 0) return false;

            break;
        }
    }
    
    if (video->videoStreamIndex == -1) return false;

    video->audioStreamIndex = -1;
    for (int i = 0; i < video->formatContext->nb_streams; i++) {
        AVStream* stream = video->formatContext->streams[i];
        if (video->audioStreamIndex == -1 && stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            video->audioStreamIndex = i;
            AVCodecParameters* codecParameters = stream->codecpar;

            const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);
            if (!codec) return false;

            video->audioCodecContext = avcodec_alloc_context3(codec);
            if (!video->audioCodecContext) return false;

            if (avcodec_parameters_to_context(video->audioCodecContext, codecParameters) < 0) return false;

            if (avcodec_open2(video->audioCodecContext, codec, NULL) < 0) return false;
            break;
        }
    }

    if(video->audioStreamIndex != -1) video->audioFrame = av_frame_alloc();
    
    video->frame = av_frame_alloc();
    video->packet = av_packet_alloc();
    return video->frame && video->packet;
}