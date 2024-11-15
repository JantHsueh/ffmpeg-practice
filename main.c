#include <stdio.h>
#include <time.h>

#include "libavutil/avutil.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include  "libswresample/swresample.h"
#include <unistd.h>


AVFormatContext* open_device()
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
    char* input_device = ":0";
    AVDictionary* options = NULL;
    // 设置双通道，todo 不起作用
    // av_dict_set(&options, "channels", "2", 0);
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


SwrContext* create_swr_context()
{
    char error[1024] = {0};

    //1、创建重采样上下文
    SwrContext* swr_context = swr_alloc();

    //2、设置重采样参数
    AVChannelLayout in_ch_layout = AV_CHANNEL_LAYOUT_MONO;
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    //设置源音频参数
    int ret = swr_alloc_set_opts2(&swr_context,
                                  &out_ch_layout, AV_SAMPLE_FMT_S16, 44100,
                                  &in_ch_layout, AV_SAMPLE_FMT_FLT, 44100,
                                  0, NULL);

    if (ret < 0)
    {
        av_strerror(ret, error, 1024);
        printf("error: %s\n", error);
        return NULL;
    }
    //5、初始化重采样上下文
    swr_init(swr_context);
    return swr_context;
}


/* check that a given sample format is supported by the encoder */
static int check_sample_fmt(const AVCodec* codec, enum AVSampleFormat sample_fmt)
{
    const enum AVSampleFormat* p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE)
    {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}


/* just pick the highest supported samplerate */
int select_sample_rate(const AVCodec* codec)
{
    const int* p;
    int best_samplerate = 0;

    if (!codec->supported_samplerates)
        return 44100;

    p = codec->supported_samplerates;
    while (*p)
    {
        if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
            best_samplerate = *p;
        p++;
    }
    return best_samplerate;
}

/* select layout with the highest channel count */
int select_channel_layout(const AVCodec* codec, AVChannelLayout* dst)
{
    const AVChannelLayout *p, *best_ch_layout;
    int best_nb_channels = 0;

    if (!codec->ch_layouts)
    {
        AVChannelLayout src = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        return av_channel_layout_copy(dst, &src);
    }


    p = codec->ch_layouts;
    while (p->nb_channels)
    {
        int nb_channels = p->nb_channels;

        if (nb_channels > best_nb_channels)
        {
            best_ch_layout = p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return av_channel_layout_copy(dst, best_ch_layout);
}

void encode(AVCodecContext* ctx, AVFrame* frame, AVPacket* pkt,
            FILE* output)
{
    int ret;

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

        fwrite(pkt->data, pkt->size, 1, output);
        av_packet_unref(pkt);
    }
}


AVCodecContext* create_encoder_context()
{
    // 创建编码器上下文
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    // 设置编码参数
    // aac 需要设置的编码格式是 AV_SAMPLE_FMT_FLTP
    codec_context->sample_fmt = AV_SAMPLE_FMT_FLTP;
    codec_context->sample_rate = 44100;
    codec_context->frame_size = 512;
    // select_channel_layout(codec, &codec_context->ch_layout);
    codec_context->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;

    // 我的理解是，设置了采样率，采样大小，声道数，那么比特率应该就是确定的，为什么还需要设置
    codec_context->bit_rate = 64000; ////AAC_LC: 128K（默认）, AAC HE: 64K, AAC HE V2: 32K
    // codec_context->profile = FF_PROFILE_AAC_LOW; // 此参数生效，一定不能设置bit_rate
    // codec_context->codec_type = AVMEDIA_TYPE_AUDIO;
    // 打开编码器
    avcodec_open2(codec_context, codec, NULL);
    return codec_context;
}


/**
 *
 * 重采样后的播放命令  ffplay -ar 44100  -f s16le audio.pcm
 *
 * 通过查看 ffplay -formats 可以查看支持的音频采样率格式 https://www.ffmpeg.org/ffplay.html#toc-Description
 * https://www.reddit.com/r/ffmpeg/comments/1edfvsx/whats_the_replacement_for_the_ac_option/
 * @return
 */
int main(void)
{
    av_log_set_level(AV_LOG_DEBUG);
    //ffmpeg 打印日志
    av_log(NULL, AV_LOG_DEBUG, "Hello, World ffmpeg!\n");

    int ret = 0;
    //调用open_device
    AVFormatContext* format_context = open_device();

    const char* file_path = "/Users/xuan/CLionProjects/ffmpeg/audio.pcm";
    // const char* file_path = "/Users/xuan/CLionProjects/ffmpeg/audio.aac";
    FILE* out_file = fopen(file_path, "wb+");
    int count = 0;
    // 音视频数据都是封装在AVPacket中，所以可以在上下文中获得AVPacket，让程序周期性的拉取音频设备的音频数据，并读取
    AVPacket packet;


    // ************* 重采样 配置 start *************
    // 创建并初始化 重采样上下文
    SwrContext* swr_context = create_swr_context();

    //1、创建输入音频数据缓冲区
    uint8_t* in_buffer;
    //输入的音频数据大小
    int in_buffer_size = 0;
    //2、分配输入音频数据缓冲区
    //AV_SAMPLE_FMT_FLT 分配的in_buffer_size大小为4096
    //AV_SAMPLE_FMT_FLTP 分配的in_buffer_size大小为2048
    //带P（plane）的数据格式在存储时，其左声道和右声道的数据是分开存储的，左声道的数据存储在data0，右声道的数据存储在data1，av_samples_alloc 返回的linesize 就是单个声道的数据大小
    //不带P（packed）的⾳频数据在存储时，是按照LRLRLR...的格式交替存储在data0中，av_samples_alloc 返回的linesize 就是所有声道的数据大小
    av_samples_alloc(&in_buffer, &in_buffer_size, 1, 512, AV_SAMPLE_FMT_FLT, 0);
    //3、创建输出音频数据缓冲区
    uint8_t* out_buffer;
    //输出的音频数据大小
    int out_buffer_size = 0;
    //4、分配输出音频数据缓冲区
    av_samples_alloc(&out_buffer, &out_buffer_size, 2, 512, AV_SAMPLE_FMT_S16, 0);
    //*****重采样 配置 end********


    // *************使用编码器编码 start ************
    // 音频输入数据

    AVCodecContext* av_codec_context = create_encoder_context();

    AVPacket* newPkt = av_packet_alloc();

    AVFrame* frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Could not allocate audio frame\n");
        exit(1);
    }
    // 设置单通道数据采样大小
    frame->nb_samples = av_codec_context->frame_size;
    // 数据采样的大小
    frame->format = av_codec_context->sample_fmt;
    // 设置对用的通道大小
    ret = av_channel_layout_copy(&frame->ch_layout, &av_codec_context->ch_layout);
    if (ret < 0)
        exit(1);

    // 设置AVFrame的buffer
    av_frame_get_buffer(frame, 0);
    // *************使用编码器编码 end ************


    while (count++ < 1000)
    {
        ret = av_read_frame(format_context, &packet);
        // ret返回-35 表示设备还没准备好, 先睡眠1s
        // device not ready, sleep 1s
        if (ret == -35)
        {
            av_log(NULL, AV_LOG_WARNING, "device not ready, wait 0.5s\n");
            av_packet_unref(&packet);
            usleep(5000);
            continue;
        }
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "read data error from device\n");
            av_packet_unref(&packet);
            break;
        }
        //单声道输出 pkt size is 2048(0x12580f400)
        //一帧音频包含1024个sample
        av_log(NULL, AV_LOG_INFO,
               "pkt size is %d(%p),count=%d \n",
               packet.size, packet.data, count);

        //把packet中数据 拷贝到in_buffer中
        memcpy(in_buffer, packet.data, packet.size);

        //重采样音频数据
        swr_convert(swr_context, &out_buffer, 512, &in_buffer, 512);

        // *************使用编码器编码 start ************

        // memcpy(frame->data[0], out_buffer, out_buffer_size);

        // encode(av_codec_context, frame, newPkt, out_file);

        fwrite(out_buffer, out_buffer_size, 1, out_file);
        fflush(out_file);
        // *************使用编码器编码 end ************

        av_packet_unref(&packet);
    }


    // encode(av_codec_context, NULL, newPkt, out_file);
    av_frame_free(&frame);
    av_packet_free(&newPkt);


    fclose(out_file);
    //7、释放重采样上下文，释放输入输出音频数据缓冲区
    av_free(in_buffer);
    av_free(out_buffer);
    swr_free(&swr_context);
    //4、关闭音频设备
    avformat_close_input(&format_context);
    av_log(NULL, AV_LOG_INFO, "finish read audio device.");
    return 0;
}
