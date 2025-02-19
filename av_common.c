#include <libavdevice/avdevice.h>
#include "av_common.h"
//
// Created by xuan on 2025/2/14.
//

AVFormatContext* open_device()
{
    char error[1024] = {0};
    //1、注册音频设备
    avdevice_register_all();

    //2、设置采集方式 avfoundtion / dshow->windows / alsa ->linux
    const AVInputFormat* input_format = av_find_input_format("avfoundation");


    // 可以是一个url，或者设备对应格式
    // mac 设备名称格式，[[video device]:[audio device]]
    // video device 0 表示摄像头，1 表示桌面
    char* input_device = "0";
    AVDictionary* options = NULL;
    // 设置双通道，todo 不起作用
    // av_dict_set(&options, "channels", "2", 0);
    // 设置比特率为64000
    // av_dict_set(&options, "b", "64000", 0);
    av_dict_set(&options, "video_size", "640x480", 0);
    av_dict_set(&options, "framerate", "30", 0);
    //设置视频格式为NV12
    av_dict_set(&options, "pixel_format", "nv12", 0);

    //通过avformat_open_input获得一个输入上下文，后续所有对于音频的操作都是在这个上下文中进行的
    AVFormatContext* format_context = NULL;
    //3、打开音频设备
    int ret = avformat_open_input(&format_context, input_device, input_format, &options);
    // 打印音频流信息
    av_dump_format(format_context, 0, input_device, 0);
    if (ret < 0)
    {
        av_strerror(ret, error, 1024);
        //打印error
        printf("error: %s\n", error);
        return NULL;
    }

    return format_context;
}


AVCodecContext* open_decoder_by_format_context(AVFormatContext *ifmtCtx, int *videoIndex)
{
    AVCodecContext* decoder_ctx;
    AVCodec* decoder;

    unsigned int i = 0;

    // 1.3 获取输入ctx
    for (i = 0; i < ifmtCtx->nb_streams; ++i)
    {
        if (ifmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            *videoIndex = i;
            break;
        }
    }

    printf("%s:%d, videoIndex = %d\n", __FUNCTION__, __LINE__, *videoIndex);

    // 1.4 查找输入解码器
    decoder = avcodec_find_decoder(ifmtCtx->streams[*videoIndex]->codecpar->codec_id);
    if (!decoder)
    {
        printf("can't find codec\n");
        return NULL;
    }

    decoder_ctx = avcodec_alloc_context3(decoder);
    if (!decoder_ctx)
    {
        printf("can't alloc codec context\n");
        return NULL;
    }

    avcodec_parameters_to_context(decoder_ctx, ifmtCtx->streams[*videoIndex]->codecpar);

    //  1.5 打开输入解码器
    if (avcodec_open2(decoder_ctx, decoder, NULL) < 0)
    {
        printf("can't open codec\n");
        return NULL;
    }
    AVStream* video_stream = ifmtCtx->streams[*videoIndex];
    // 检查时间基
    if (video_stream->time_base.num != 0 && video_stream->time_base.den != 0)
    {
        decoder_ctx->time_base = video_stream->time_base;
    }
    // else if (ifmtCtx->iformat->flags & AVFMT_SHOW_IDS)
    // {
    //     av_codec_ctx_in->time_base = (AVRational){1, 90000};
    // }
    else
    {
        decoder_ctx->time_base = (AVRational){1, 25}; // 默认时间基
    }

    return decoder_ctx;
}


AVCodecContext* open_h264_encoder(int width, int height)
{
    AVCodecContext* encoder_codec_ctx;

    AVCodec* pH264Codec;
    AVDictionary* params = NULL;

    // 1.6 打开H264编码器
    pH264Codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!pH264Codec)
    {
        printf("can't find h264 codec.\n");
        return NULL;
    }

    // 1.6.1 设置参数
    encoder_codec_ctx = avcodec_alloc_context3(pH264Codec);
    encoder_codec_ctx->codec_id = AV_CODEC_ID_H264;
    encoder_codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    encoder_codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    encoder_codec_ctx->width = width;
    encoder_codec_ctx->height = height;
    // encoder_codec_ctx->time_base.num = 1;
    // encoder_codec_ctx->time_base.den = 25; //帧率（即一秒钟多少张图片）


    // 设置B帧，减少码流
    // encoder_codec_ctx->max_b_frames = 1; // option  最大B帧数
    encoder_codec_ctx->has_b_frames = 1; // option  1 表示有B帧，0表示没有B帧，更低的延时

    // 设置帧率
    encoder_codec_ctx->time_base = (AVRational){1, 30};
    encoder_codec_ctx->framerate = (AVRational){30, 1};

    encoder_codec_ctx->bit_rate = 400000; //比特率（调节这个大小可以改变编码后视频的质量）
    encoder_codec_ctx->gop_size = 50;
    // encoder_codec_ctx->qmin = 10;
    // encoder_codec_ctx->qmax = 51;
    //some formats want stream headers to be separate
    //	if (pH264CodecCtx->flags & AVFMT_GLOBALHEADER)
    {
        encoder_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }


    // 1.7 打开H.264编码器
    // av_dict_set(&params, "preset", "superfast", 0);
    // av_dict_set(&params, "tune", "zerolatency", 0); //实现实时编码
    if (avcodec_open2(encoder_codec_ctx, pH264Codec, &params) < 0)
    {
        printf("can't open video encoder.\n");
        return NULL;
    }
    return encoder_codec_ctx;
}


AVFormatContext* init_output_format_context(const char* out_filename, AVCodecContext* h264_encoder_ctx)
{
    AVFormatContext* ofmt_ctx = NULL;
    const char* inFilename = "0"; //输入URL
    const char* ofmtName = NULL;
    int ret = 0;

    //
    if (strstr(out_filename, "rtmp://"))
    {
        ofmtName = "flv";
    }
    else if (strstr(out_filename, "udp://"))
    {
        ofmtName = "mpegts";
    }
    else
    {
        ofmtName = NULL;
    }

    avformat_alloc_output_context2(&ofmt_ctx, NULL, ofmtName, out_filename);
    if (!ofmt_ctx)
    {
        printf("can't create output context\n");
        return NULL;
    }

    //  创建输出流
    AVStream* out_stream = avformat_new_stream(ofmt_ctx, NULL);
    if (!out_stream)
    {
        printf("failed to allocate output stream\n");
        return NULL;
    }

    avcodec_parameters_from_context(out_stream->codecpar, h264_encoder_ctx);
    out_stream->time_base = h264_encoder_ctx->time_base;


    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
    {
        // 2.3 创建并初始化一个AVIOContext, 用以访问URL（outFilename）指定的资源
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            printf("can't open output URL: %s\n", out_filename);
            return NULL;
        }
    }


    //  写输出文件头，会设置ofmtCtx->streams[0]->time_base.den = 15360
    if (avformat_write_header(ofmt_ctx, NULL) < 0)
    {
        printf("Error accourred when opening output file\n");
        return NULL;
    }

    return ofmt_ctx;
}


AVFrame* create_writable_frame(int width, int height, enum AVPixelFormat format)
{
    AVFrame* frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    frame->format = format;
    frame->width = width;
    frame->height = height;


    //// 分配内存
    // outBuffer = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, av_decoder_ctx->width, av_decoder_ctx->height, 1));
    // av_image_fill_arrays(converted_frame->data, converted_frame->linesize, outBuffer, AV_PIX_FMT_YUV420P, av_decoder_ctx->width, av_decoder_ctx->height, 1);


    // 分配内存
    int ret = av_frame_get_buffer(frame, 32); // 按照32位对其
    if (ret < 0)
    {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }



    ret = av_frame_make_writable(frame);
    if (ret < 0) {
        fprintf(stderr, "Could not make the destination frame writable\n");
        av_frame_free(&frame);
        exit(1);
    }

    return frame;
}





FILE* open_file(const char* filename)
{
    FILE* file = fopen(filename, "wb");
    if (!file)
    {
        fprintf(stderr, "Could not open file %s\n", filename);
        exit(1);
    }
    return file;
}
