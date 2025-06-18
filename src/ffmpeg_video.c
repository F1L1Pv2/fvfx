#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "ffmpeg_video.h"
#include "engine/vulkan_globals.h"
#include "engine/vulkan_helpers.h"
#include "engine/vulkan_images.h"
#include "sound_engine.h"
#include "engine/platform.h"

static bool initializeVideoContext(Video* video, const char* filename);
static bool initializeDecoder(Video* video);

bool ffmpegInit(const char* filename, Video* video) 
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
    ffmpegUninit(video);
    return false;
}

bool ffmpegGetFrame(Video* video, Frame* frame, bool skip_audio) {
    av_frame_unref(video->frame);
    int response;
    while (av_read_frame(video->formatContext, video->packet) >= 0) {
        if(skip_audio){
            if (video->packet->stream_index != video->videoStreamIndex) {
                av_packet_unref(video->packet);
                continue;
            }
        }else{
            if (!(video->packet->stream_index == video->videoStreamIndex || video->packet->stream_index == video->audioStreamIndex)) {
                av_packet_unref(video->packet);
                continue;
            }
        }

        if(video->packet->stream_index == video->videoStreamIndex){
            response = avcodec_send_packet(video->codecContextVideo, video->packet);
        }else{
            response = avcodec_send_packet(video->codecContextAudio, video->packet);
        }
        if (response < 0) {
            av_packet_unref(video->packet);
            continue;
        }

        if(video->packet->stream_index == video->videoStreamIndex){
            response = avcodec_receive_frame(video->codecContextVideo, video->frame);
        }else{
            response = avcodec_receive_frame(video->codecContextAudio, video->frame);
        }
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            av_packet_unref(video->packet);
            continue;
        } 
        else if (response < 0) {
            av_packet_unref(video->packet);
            return false;
        }

        if(video->packet->stream_index == video->videoStreamIndex){
            if(frame->width == 0 && frame->height == 0 && frame->data == NULL){
                video->swsContext = sws_getContext(
                    video->frame->width, video->frame->height, video->codecContextVideo->pix_fmt,
                    video->frame->width, video->frame->height, AV_PIX_FMT_RGBA,
                    SWS_FAST_BILINEAR, NULL, NULL, NULL
                );
                frame->width = video->frame->width;
                frame->height = video->frame->height;
                frame->data = malloc(frame->width*frame->height*sizeof(uint32_t));
            }
    
            // Convert frame to RGB
            uint8_t* dest[4] = {(uint8_t*)frame->data, NULL, NULL, NULL};
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
        }else{
            float* data = malloc(video->frame->nb_samples * soundEngineGetChannels() * sizeof(float));
            uint8_t* out_data[1] = { (uint8_t*)data };
            int out_samples = swr_convert(video->swrContext,
                out_data, video->frame->nb_samples,
                (const uint8_t**)video->frame->data, video->frame->nb_samples);
    
            if (out_samples < 0) {
                free(data);
                continue;
            }
    
            AudioFrame audioFrame = {
                .data = data,
                .numberSamples = out_samples,
            };
            
            soundEngineEnqueueFrame(&audioFrame);
        }

        av_packet_unref(video->packet);
    }

    return false;
}

bool ffmpegSeek(Video* video, Frame* frame, double time_seconds) {
    int64_t target_pts = time_seconds / av_q2d(video->formatContext->streams[video->videoStreamIndex]->time_base);

    int ret = av_seek_frame(video->formatContext, video->videoStreamIndex, target_pts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        printf("Seek failed: %s\n", av_err2str(ret));
        return false;
    }

    avcodec_flush_buffers(video->codecContextVideo);
    if(video->codecContextAudio) {
        avcodec_flush_buffers(video->codecContextAudio);
        soundEngineResetQueue();
    }
 
    do {
        if (!ffmpegGetFrame(video, frame, true)) break;
    } while(frame->frameTime < time_seconds);

    return true;
}

void ffmpegUninit(Video* video) {
    if (video->swsContext) sws_freeContext(video->swsContext);
    if (video->codecContextVideo) avcodec_free_context(&video->codecContextVideo);
    if (video->codecContextAudio) avcodec_free_context(&video->codecContextAudio);
    if (video->frame) av_frame_free(&video->frame);
    if (video->packet) av_packet_free(&video->packet);
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
    video->audioStreamIndex = -1;
    for (int i = 0; i < video->formatContext->nb_streams; i++) {
        AVStream* stream = video->formatContext->streams[i];
        if (video->videoStreamIndex == -1 && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video->videoStreamIndex = i;
            AVCodecParameters* codecParameters = stream->codecpar;

            const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);
            if (!codec) return false;
            
            video->codecContextVideo = avcodec_alloc_context3(codec);
            if (!video->codecContextVideo) return false;
            
            if (avcodec_parameters_to_context(video->codecContextVideo, codecParameters) < 0) return false;
            
            if (avcodec_open2(video->codecContextVideo, codec, NULL) < 0) return false;
        }else if(video->audioStreamIndex == -1 && stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            video->audioStreamIndex = i;
            AVCodecParameters* codecParameters = stream->codecpar;

            const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);
            if (!codec) return false;
            
            video->codecContextAudio = avcodec_alloc_context3(codec);
            if (!video->codecContextAudio) return false;
            
            if (avcodec_parameters_to_context(video->codecContextAudio, codecParameters) < 0) return false;
            
            if (avcodec_open2(video->codecContextAudio, codec, NULL) < 0) return false;

            if(!initSoundEngine()) return false;

            int ret = swr_alloc_set_opts2(&video->swrContext,
                soundEngineGetChannels() == 2 ? &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO : &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO,
                AV_SAMPLE_FMT_FLT,
                soundEngineGetSampleRate(),
                &video->codecContextAudio->ch_layout,
                video->codecContextAudio->sample_fmt,
                codecParameters->sample_rate,
                0,
                NULL);

            if (!video->swrContext || swr_init(video->swrContext) < 0) {
                fprintf(stderr, "Failed to initialize resampler\n");
                return false;
            }
        }
    }
    
    if (video->videoStreamIndex == -1) return false;
    
    video->frame = av_frame_alloc();
    video->packet = av_packet_alloc();
    return video->frame && video->packet;
}