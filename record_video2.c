#include <stdio.h>
#include <time.h>

#include "libavutil/avutil.h"
#include "libavutil/timestamp.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include  "libswresample/swresample.h"
#include <unistd.h>
#include <sys/time.h>

static AVFormatContext* open_device()
{
    char error[1024] = {0};
    //1、注册音频设备
    avdevice_register_all();

    //2、设置采集方式 avfoundtion / dshow->windows / alsa ->linux
    const AVInputFormat* input_format = av_find_input_format("avfoundation");

    //通过avformat_open_input获得一个输入上下文，后续所有对于音频的操作都是在这个上下文中进行的
    AVFormatContext* format_context = NULL;
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


static void encode(AVCodecContext* ctx, AVFrame* frame, AVPacket* pkt, FILE* output)
{
    int ret;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3"PRId64"\n", frame->pts);

    /* send the frame for encoding */
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0)
    {
        fprintf(stderr, "Error sending the frame to the encoder\n");
        exit(1);
    }

    /* read all the available output packets (in general there may be any
     * number of them */
    while (ret >= 0)
    {
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0)
        {
            fprintf(stderr, "Error encoding audio frame\n");
            exit(1);
        }
        printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);


        fwrite(pkt->data, 1, pkt->size, output);
        av_packet_unref(pkt);
    }
}


static void encode_by_stream(AVCodecContext* ctx, AVFrame* frame, AVPacket* pkt, AVFormatContext* ofmt_ctx, AVStream* out_stream)
{
    int ret;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3"PRId64"\n", frame->pts);

    /* send the frame for encoding */
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0)
    {
        fprintf(stderr, "Error sending the frame to the encoder\n");
        exit(1);
    }

    /* read all the available output packets (in general there may be any
     * number of them */
    while (ret >= 0)
    {
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0)
        {
            fprintf(stderr, "Error encoding audio frame\n");
            exit(1);
        }
        printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);


        /* copy packet */
        av_packet_rescale_ts(pkt, ctx->time_base, out_stream->time_base);
        pkt->pos = -1;
        // log_packet(ofmt_ctx, pkt, "out");

        ret = av_interleaved_write_frame(ofmt_ctx, pkt);
        /* pkt is now blank (av_interleaved_write_frame() takes ownership of
         * its contents and resets pkt), so that no unreferencing is necessary.
         * This would be different if one used av_write_frame(). */
        if (ret < 0)
        {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }

        av_packet_unref(pkt);
    }
}


/**
 *
 * 打开视频编码器
 */
static void open_video_encoder(int width, int height, AVCodecContext** codec_context)
{
    //1、查找编码器
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
    {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    //2、创建编码器上下文
    *codec_context = avcodec_alloc_context3(codec);
    if (!(*codec_context))
    {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    //3、设置编码器参数

    //sps
    //
    // (*codec_context)->profile = FF_PROFILE_H264_HIGH_444;
    // (*codec_context)->level = 50;
    av_opt_set((*codec_context)->priv_data, "preset", "slow", 0);

    (*codec_context)->width = width;
    (*codec_context)->height = height;

    (*codec_context)->gop_size = 10; // 两个I帧之间的帧数量
    // (*codec_context)->keyint_min = 25; // option 两个I帧之间的最小帧数, 如果两个帧之间差别大，可以插入一个关键帧

    // 设置B帧，减少码流
    (*codec_context)->max_b_frames = 1; // option  最大B帧数
    // (*codec_context)->has_b_frames = 1; // option  1 表示有B帧，0表示没有B帧，更低的延时
    (*codec_context)->refs = 5; // option

    (*codec_context)->pix_fmt = AV_PIX_FMT_YUV420P;

    (*codec_context)->bit_rate = 400000;
    // c->bit_rate = 400000;

    // 设置帧率
    (*codec_context)->time_base = (AVRational){1, 30};
    (*codec_context)->framerate = (AVRational){30, 1};

    // (*codec_context)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // (*codec_context)->ticks_per_frame = 1;
    //4、打开编码器
    if (avcodec_open2(*codec_context, codec, NULL) < 0)
    {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    av_log(NULL, AV_LOG_INFO, "Encoder time_base: %d/%d\n",
           (*codec_context)->time_base.num, (*codec_context)->time_base.den);
}


/**
 * 创建视频帧
 */
static AVFrame* create_video_frame(int width, int height)
{
    AVFrame* frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = width;
    frame->height = height;

    // 分配内存
    int ret = av_frame_get_buffer(frame, 32); // 按照32位对其
    if (ret < 0)
    {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }

    return frame;
}


static FILE* open_file(char* file_path)
{
    // 判断文件是否存在，如果存在，就删除
    remove(file_path);

    FILE* out_file = fopen(file_path, "wb+");
    return out_file;
}

static void log_packet(const AVFormatContext* fmt_ctx, const AVPacket* pkt, const char* tag)
{
    AVRational* time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}


/**
 * 录制视频
 * 播放命令 ffplay -pixel_format uyvy422 -video_size 640x480 v.yuv
 * 如果不设置-pixel_format uyvy422，视频会花屏
 */
void record_video2()
{
    int ret = 0;
    int count = 0;
    int success_count = 0;

    //调用open_device
    AVFormatContext* av_format_context_in = open_device();

    AVCodecContext* video_codec_context = NULL;
    open_video_encoder(640, 480, &video_codec_context);

    // avformat_find_stream_info(av_format_context_in,NULL);

    AVFormatContext* av_format_context_out = NULL;

    // FILE* out_file = open_file("/Users/xuan/CLionProjects/ffmpeg/v.yuv");
    // FILE* out_file_264 = open_file("/Users/xuan/CLionProjects/ffmpeg/v.h264");
    char* out_file_mp4 = "/Users/xuan/CLionProjects/ffmpeg/2.mp4";


    avformat_alloc_output_context2(&av_format_context_out, NULL, "mp4", out_file_mp4);


    if (!av_format_context_out)
    {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        return;
    }

    const AVOutputFormat* ofmt = av_format_context_out->oformat;

    AVStream* out_stream = avformat_new_stream(av_format_context_out, NULL);
    if (!out_stream)
    {
        fprintf(stderr, "Failed allocating output stream\n");
        ret = AVERROR_UNKNOWN;
        return;
    }

    // ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
    // if (ret < 0)
    // {
    //     fprintf(stderr, "Failed to copy codec parameters\n");
    //     return;
    // }
    // Copy codec parameters from codec context to stream
    ret = avcodec_parameters_from_context(out_stream->codecpar, video_codec_context);
    if (ret < 0)
    {
        fprintf(stderr, "Failed to copy codec parameters\n");
        return;
    }

    // Set the time base for the output stream
    out_stream->time_base = video_codec_context->time_base;

    out_stream->codecpar->codec_tag = 0;
    av_dump_format(av_format_context_out, 0, out_file_mp4, 1);


    AVFrame* av_frame = create_video_frame(640, 480);

    // 创建编码后的输出packet
    AVPacket* pkt = av_packet_alloc();
    AVPacket* new_pkt = av_packet_alloc();
    int i = 0;

    int64_t frame_interval = 0; // 计算每帧时间间隔 (us)



    if (!(ofmt->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&av_format_context_out->pb, out_file_mp4, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            fprintf(stderr, "Could not open output file '%s'", out_file_mp4);
            return;
        }
    }


    ret = avformat_write_header(av_format_context_out, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "Error occurred when opening output file\n");
        return;
    }

    // 1、av_read_frame获取到原始数据，
    // 2、手动转换成YUV420P格式
    // 3、用h264编码器编码
    while (count++ < 1000)
    {
        ret = av_read_frame(av_format_context_in, pkt);

        // ret返回-35 表示设备还没准备好, 先睡眠1s
        // device not ready, sleep 1s
        if (ret == -35)
        {
            // av_log(NULL, AV_LOG_WARNING, "device not ready, wait 0.5s\n");
            av_packet_unref(pkt);
            usleep(5000);
            continue;
        }
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "read data error from device\n");
            av_packet_unref(pkt);
            break;
        }

        if (ret == 0)
        {
            av_log(NULL, AV_LOG_INFO,
                   "pkt size is %d(%p),count=%d, success_count=%d\n",
                   pkt->size, pkt->data, count, success_count++);
            // 如果pkt->size不对，会导致视频画面滚动
            // 程序自动设置了，视频格式为 uyvy422, 640x480x2 = 614400
            // 虽然ret == -35，睡眠了0.5s，但每次返回的是一个视频帧
            // fwrite(pkt->data, pkt->size, 1, out_file);
            // fflush(out_file);


            //把NV12 数据转为YUV420
            memcpy(av_frame->data[0], pkt->data, 307200);

            for (i = 0; i < 307200 / 4; i++)
            {
                av_frame->data[1][i] = pkt->data[307200 + i * 2];
                av_frame->data[2][i] = pkt->data[307200 + i * 2 + 1];
            }
            // fwrite(av_frame->data[0], 307200, 1, out_file);
            // fwrite(av_frame->data[1], 307200 / 4, 1, out_file);
            // fwrite(av_frame->data[2], 307200 / 4, 1, out_file);
            // fflush(out_file);

            // 调试日志
            av_log(NULL, AV_LOG_INFO, "Frame count: %d, PTS: %ld, Frame Interval: %ld us\n",
                   count, av_frame->pts, frame_interval);


            // av_frame->pts = success_count * av_packet_rescale_tale_q(1, av_inv_q(time_base), time_base;;
            // av_frame->pts = success_count * av_rescale_q(1, (AVRational){1, 30}, video_codec_context->time_base);

            // av_frame->pts = success_count * (video_codec_context->time_base.den / video_codec_context->time_base.num / 30);
            av_frame->pts = av_rescale_q(success_count, (AVRational){1, 30}, video_codec_context->time_base);

            av_log(NULL, AV_LOG_INFO, "Frame %d PTS: %ld\n", success_count, av_frame->pts);

            // 编码并写入文件
            encode_by_stream(video_codec_context, av_frame, new_pkt, av_format_context_out, out_stream);
        }
    }
    av_write_trailer(av_format_context_out);

    //把缓冲区的数据刷新到文件
    // encode(video_codec_context, NULL, pkt, out_file_264);

    // fclose(out_file_264);
    // fclose(out_file);
    // fclose(out_file_mp4);
}

