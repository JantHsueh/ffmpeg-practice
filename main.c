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
                                  &out_ch_layout, AV_SAMPLE_FMT_S16, 48000,
                                  &in_ch_layout, AV_SAMPLE_FMT_FLT, 48000,
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

        fwrite(pkt->data, 1, pkt->size, output);
        av_packet_unref(pkt);
    }
}


AVCodecContext* create_encoder_context()
{
    // 创建编码器上下文
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MP2);
    // const AVCodec* codec = avcodec_find_encoder_by_name("libfdk_aac");
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    // 设置编码参数

    codec_context->sample_fmt = AV_SAMPLE_FMT_S16;
    // codec_context->bit_rate = 64000; ////AAC_LC: 128K（默认）, AAC HE: 64K, AAC HE V2: 32K

    if (!check_sample_fmt(codec, codec_context->sample_fmt))
    {
        fprintf(stderr, "Encoder does not support sample format %s",
                av_get_sample_fmt_name(codec_context->sample_fmt));
        exit(1);
    }

    // select_channel_layout(codec, &codec_context->ch_layout);


    // aac 需要设置的编码格式是 AV_SAMPLE_FMT_FLTP
    /* select other audio parameters supported by the encoder */
    // codec_context->sample_rate = select_sample_rate(codec);
    codec_context->sample_rate = 48000;
    //不起作用
    // codec_context->frame_size = 1152;
    // select_channel_layout(codec, &codec_context->ch_layout);
    codec_context->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;

    // 我的理解是，设置了采样率，采样大小，声道数，那么比特率应该就是确定的，为什么还需要设置
    // codec_context->bit_rate = 64000; ////AAC_LC: 128K（默认）, AAC HE: 64K, AAC HE V2: 32K
    // codec_context->profile = FF_PROFILE_AAC_LOW; // 此参数生效，一定不能设置bit_rate
    // codec_context->codec_type = AVMEDIA_TYPE_AUDIO;
    // 打开编码器
    avcodec_open2(codec_context, codec, NULL);
    return codec_context;
}


AVFrame* alloc_audio_frame(enum AVSampleFormat sample_fmt,
                           AVChannelLayout ch_layout, int sample_rate,
                           int nb_samples)
{
    AVFrame* frame = av_frame_alloc();
    int ret;

    if (!frame)
    {
        fprintf(stderr, "Error allocating an audio frame\n");
        exit(1);
    }
    // 多次测试发现，带编码的帧，需要设置好这些参数，之前没有设置sample_rate，导致保存的音频总是有问题，滋滋响
    frame->format = sample_fmt;
    frame->ch_layout = ch_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples)
    {
        ret = av_frame_get_buffer(frame, 0);

        if (ret < 0)
        {
            fprintf(stderr, "Error allocating an audio buffer\n");
            exit(1);
        }
    }

    return frame;
}


/**
 *
 * 打开视频编码器
 */
void open_video_encoder(int width, int height, AVCodecContext** codec_context)
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

    (*codec_context)->width = width;
    (*codec_context)->height = height;

    (*codec_context)->gop_size = 250; // 两个I帧之间的帧数量
    (*codec_context)->keyint_min = 250; // option 两个I帧之间的最小帧数, 如果两个帧之间差别大，可以插入一个关键帧

    // 设置B帧，减少码流
    (*codec_context)->max_b_frames = 3; // option  最大B帧数
    (*codec_context)->has_b_frames = 1; // option  1 表示有B帧，0表示没有B帧，更低的延时
    (*codec_context)->refs = 3; // option

    (*codec_context)->pix_fmt = AV_PIX_FMT_YUV420P;

    (*codec_context)->bit_rate = 600000;

    // 设置帧率
    (*codec_context)->time_base = (AVRational){1, 25};
    (*codec_context)->framerate = (AVRational){25, 1};

    //4、打开编码器
    if (avcodec_open2(*codec_context, codec, NULL) < 0)
    {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }
}


/**
 * 创建视频帧
 */
AVFrame* create_video_frame(int width, int height)
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


int recoder_audio()
{
    av_log_set_level(AV_LOG_DEBUG);
    //ffmpeg 打印日志
    av_log(NULL, AV_LOG_DEBUG, "Hello, World ffmpeg!\n");

    int ret = 0;
    //调用open_device
    AVFormatContext* format_context = open_device();
    AVCodecContext* video_codec_context = NULL;


    int audio_stream_index = -1; // microphone input audio stream index
    AVCodec* audio_decoder = NULL;
    AVCodecContext* audio_dec_ctx = NULL;
    // 寻找音频流
    for (int i = 0; i < format_context->nb_streams; i++)
    {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_stream_index = i;
            break;
        }
    }

    if (audio_stream_index == -1)
    {
        fprintf(stderr, "%s could not find an audio stream.\n", __FUNCTION__);
        return -1;
    }

    // std::cout << __FUNCTION__ << ": find audio stream success. audio_stream_index: " << audio_stream_index << std::endl;;
    printf("%s: find audio stream success. audio_stream_index: %d\n", __FUNCTION__, audio_stream_index);
    // 获取 audio stream
    AVStream* audio_stream = format_context->streams[audio_stream_index];
    // 根据codec id获取codec
    audio_decoder = (AVCodec*)avcodec_find_decoder(format_context->streams[audio_stream_index]->codecpar->codec_id);

    if (audio_decoder == NULL)
    {
        fprintf(stderr, "%s: can not find an audio codec.\n", __FUNCTION__);
        return -1;
    }

    printf("audio decoder: %s, codec id: %d, codec long name: %s\n", audio_decoder->name, audio_decoder->id, audio_decoder->long_name);
    // 初始化解码器上下文
    audio_dec_ctx = (AVCodecContext*)avcodec_alloc_context3(audio_decoder);
    // 复制参数
    avcodec_parameters_to_context(audio_dec_ctx, audio_stream->codecpar);

    if (avcodec_open2(audio_dec_ctx, audio_decoder, NULL) < 0)
    {
        fprintf(stderr, "%s can not open a audio codec.\n", __FUNCTION__);
        return -1;
    }

    printf("%s: initialize audio decoder success.\n", __FUNCTION__);
    // av_dump_format(format_context, 0, ":0", 0);
    // show_audio_input_ctx(audio_stream);


    const char* file_path = "/Users/xuan/CLionProjects/ffmpeg/audio.pcm";
    // const char* file_path = "/Users/xuan/CLionProjects/ffmpeg/audio.aac";
    // 判断文件是否存在，如果存在，就删除
    remove(file_path);

    FILE* out_file = fopen(file_path, "wb+");
    int count = 0;
    // 音视频数据都是封装在AVPacket中，所以可以在上下文中获得AVPacket，让程序周期性的拉取音频设备的音频数据，并读取
    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();


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
    // int out_frame_nb_samples = av_rescale_rnd(512, 44100, 48000, AV_ROUND_UP);
    AVFrame* out_frame = alloc_audio_frame(av_codec_context->sample_fmt, av_codec_context->ch_layout, 48000, av_codec_context->frame_size);;
    AVPacket* newPkt = av_packet_alloc();
    // *************使用编码器编码 end ************


    while (count++ < 1000)
    {
        ret = av_read_frame(format_context, pkt);

        // ret返回-35 表示设备还没准备好, 先睡眠1s
        // device not ready, sleep 1s
        if (ret == -35)
        {
            av_log(NULL, AV_LOG_WARNING, "device not ready, wait 0.5s\n");
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
            if (pkt->stream_index == audio_stream_index)
            {
                ret = avcodec_send_packet(audio_dec_ctx, pkt);

                while (ret >= 0)
                {
                    ret = avcodec_receive_frame(audio_dec_ctx, frame);

                    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                    {
                        break;
                    }
                    else if (ret < 0)
                    {
                        fprintf(stderr, "avcodec_receive_frame failed\n");
                        return -1;
                    }


                    //单声道输出 pkt size is 2048(0x12580f400)
                    //一帧音频包含1024个sample
                    av_log(NULL, AV_LOG_INFO,
                           "pkt size is %d(%p),count=%d \n",
                           pkt->size, pkt->data, count);

                    //把packet中数据 拷贝到in_buffer中
                    memcpy(in_buffer, pkt->data, pkt->size);
                    //重采样音频数据
                    // 这里的 512 是指一个该通道的样本数量
                    swr_convert(swr_context, &out_buffer, 512, &in_buffer, 512);
                    ret = swr_convert_frame(swr_context, out_frame, frame);

                    // *************使用编码器编码 start ************

                    // av_frame_make_writable(out_frame);
                    // memcpy((void *)out_frame->data[0], out_buffer, out_buffer_size);

                    // encode(av_codec_context, out_frame, newPkt, out_file);

                    fwrite(out_buffer, out_buffer_size, 1, out_file);
                    fflush(out_file);
                    // *************使用编码器编码 end ************

                    av_packet_unref(pkt);
                }
            }
        }
    }


    encode(av_codec_context, NULL, newPkt, out_file);
    av_frame_free(&out_frame);
    av_packet_free(&newPkt);


    fclose(out_file);
    //7、释放重采样上下文，释放输入输出音频数据缓冲区
    av_free(in_buffer);
    av_free(out_buffer);
    swr_free(&swr_context);
    //4、关闭音频设备
    avformat_close_input(&format_context);
    av_log(NULL, AV_LOG_INFO, "finish read audio device.");
}

/**
 * 录制视频
 * 播放命令 ffplay -pixel_format uyvy422 -video_size 640x480 v.yuv
 * 如果不设置-pixel_format uyvy422，视频会花屏
 */
void recoder_video()
{
    int ret = 0;
    int count = 0;
    int success_count = 0;

    //调用open_device
    AVFormatContext* format_context = open_device();
    AVCodecContext* video_codec_context = NULL;

    open_video_encoder(640, 480, &video_codec_context);

    const char* file_path = "/Users/xuan/CLionProjects/ffmpeg/v.yuv";
    // const char* file_path = "/Users/xuan/CLionProjects/ffmpeg/audio.aac";
    // 判断文件是否存在，如果存在，就删除
    remove(file_path);

    FILE* out_file = fopen(file_path, "wb+");

    //
    AVFrame* av_frame = create_video_frame(640, 480);

    // 创建编码后的输出packet
    AVPacket* pkt = av_packet_alloc();
    int i = 0;
    while (count++ < 1000)
    {
        ret = av_read_frame(format_context, pkt);

        // ret返回-35 表示设备还没准备好, 先睡眠1s
        // device not ready, sleep 1s
        if (ret == -35)
        {
            av_log(NULL, AV_LOG_WARNING, "device not ready, wait 0.5s\n");
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
            fwrite(av_frame->data[0], 307200, 1, out_file);
            fwrite(av_frame->data[1], 307200 / 4, 1, out_file);
            fwrite(av_frame->data[2], 307200 / 4, 1, out_file);
            // fflush(out_file);


            av_packet_unref(pkt);
        }
    }
}


/**
 *
 * 重采样后的播放命令
 * 播放pcm音频，必须设置采样率、采样大小、通道数(默认单声道)
 * ffplay -ar 44100  -f f32le audio.pcm
 * ffplay -ar 44100 -ch_layout stereo -f s16le audio.pcm
 * https://fftrac-bg.ffmpeg.org/ticket/11077
 *
 * 通过查看 ffplay -formats 可以查看支持的音频采样率格式 https://www.ffmpeg.org/ffplay.html#toc-Description
 * https://www.reddit.com/r/ffmpeg/comments/1edfvsx/whats_the_replacement_for_the_ac_option/
 * @return
 */
int main(void)
{
    recoder_video();
    return 0;
}
