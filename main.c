#include <stdio.h>
#include <time.h>

#include "libavutil/avutil.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include  "libswresample/swresample.h"
#include <unistd.h>

/**
 *
 * 重采样后的播放命令  ffplay -ar 44100  -f s16le audio.pcm
 *
 * 通过查看 ffplay -formats 可以查看支持的音频采样率格式 https://www.ffmpeg.org/ffplay.html#toc-Description
 * @return 
 */
int main(void)
{
    av_log_set_level(AV_LOG_DEBUG);
    //ffmpeg 打印日志
    av_log(NULL, AV_LOG_DEBUG, "Hello, World ffmpeg!\n");

    char error[1024] = {0};
    int ret = 0;
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
    ret = avformat_open_input(&format_context, input_device, input_format, &options);
    // 打印音频流信息
    av_dump_format(format_context, 0, input_device, 0);
    if (ret < 0)
    {
        av_strerror(ret, error, 1024);
        //打印error
        printf("error: %s\n", error);
        return -1;
    }

    // 音视频数据都是封装在AVPacket中，所以可以在上下文中获得AVPacket，让程序周期性的拉取音频设备的音频数据，并读取
    AVPacket packet;
    int count = 0;
    const char* file_path = "/Users/xuan/CLionProjects/ffmpeg/audio.pcm";
    FILE* out_file = fopen(file_path, "wb+");


    //创建重采样音频数据
    //1、创建重采样上下文
    SwrContext* swr_context = swr_alloc();
    //4、设置重采样参数

    AVChannelLayout in_ch_layout = AV_CHANNEL_LAYOUT_MONO;

    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    //设置源音频参数
    ret = swr_alloc_set_opts2(&swr_context,
                                      &out_ch_layout, AV_SAMPLE_FMT_S16, 44100,
                                      &in_ch_layout, AV_SAMPLE_FMT_FLT, 44100,
                                      0, NULL);

    if (ret < 0) {
        av_strerror(ret, error, 1024);
        printf("error: %s\n", error);
        return -1;
    }
    //5、初始化重采样上下文
    swr_init(swr_context);
    //6、重采样音频数据 开始


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
    //5、重采样音频数据
    //6、重采样音频数据 结束



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


        fwrite(out_buffer, out_buffer_size, 1, out_file);
        fflush(out_file);
        av_packet_unref(&packet);
    }
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


