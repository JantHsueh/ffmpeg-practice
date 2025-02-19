#ifndef AV_COMMON_H
#define AV_COMMON_H

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include "libavformat/avformat.h"


// Declare the _open_file function
FILE* open_file(const char* filename);
AVFormatContext* open_device();
AVCodecContext* open_decoder_by_format_context(AVFormatContext* ifmtCtx, int* videoIndex);
AVCodecContext* open_h264_encoder(int width, int height);
AVFormatContext* init_output_format_context(const char* out_filensame, AVCodecContext* h264_encoder_ctx);
AVFrame* create_writable_frame(int width, int height, enum AVPixelFormat format);

#endif // AV_COMMON_H
