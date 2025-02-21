#ifndef AV_COMMON_H
#define AV_COMMON_H

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include "libavformat/avformat.h"


// Declare the _open_file function
FILE* open_file(const char* filename);
AVFormatContext* open_device();
AVCodecContext* open_decoder_by_format_context(AVFormatContext* ifmtCtx, int* videoIndex, enum AVMediaType type);
AVCodecContext* open_encoder_h264(int width, int height);
AVCodecContext* open_encoder_audio();
AVFrame* create_writable_video_frame(int width, int height, enum AVPixelFormat format);
AVFrame* create_writable_audio_frame(AVChannelLayout ch_layout, int nb_samples, enum AVPixelFormat format);
AVFormatContext* init_output_format_context(const char* out_filensame, AVCodecContext* encoder_ctx);
void create_writable_frame(AVFrame* frame);

#endif // AV_COMMON_H
