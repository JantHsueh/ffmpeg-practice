#include <unistd.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavutil/channel_layout.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>

#include "av_common.h"


static int initialize_resampler(SwrContext** swr_ctx,
                                AVChannelLayout* out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate,
                                AVChannelLayout* in_ch_layout, enum AVSampleFormat in_sample_fmt, int in_sample_rate)
{
    int ret = swr_alloc_set_opts2(swr_ctx, // ctx
                                  out_ch_layout, // 输出的channel 的布局
                                  out_sample_fmt, // 输出的采样格式
                                  out_sample_rate, // 输出的采样率
                                  in_ch_layout, // 输入的channel布局
                                  in_sample_fmt, // 输入的采样格式
                                  in_sample_rate, // 输入的采样率
                                  0, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "Could not allocate resampler context\n");
        return -1;
    }

    if (swr_init(*swr_ctx) < 0)
    {
        fprintf(stderr, "Could not initialize the resampling context\n");
        swr_free(swr_ctx);
        return -1;
    }

    return 0; // 成功
}


static void encode_and_write_frame(AVCodecContext* encoder_ctx, AVFrame* swr_frame, AVFormatContext* ofmtCtx, AVPacket* out_packet, int frameIndex)
{
    int ret = avcodec_send_frame(encoder_ctx, swr_frame);


    if (ret < 0)
    {
        // 获取错误信息
        char error[1024] = {0};
        av_strerror(ret, error, 1024);
    }

    // 将重采样后的帧发送给编码器
    if (ret == 0)
    {
        while (avcodec_receive_packet(encoder_ctx, out_packet) == 0)
        {
            // 正确设置数据包中的流索引
            out_packet->stream_index = ofmtCtx->streams[0]->index;

            // 调整时间戳，使其基于输出流的时间基
            av_packet_rescale_ts(out_packet, encoder_ctx->time_base, ofmtCtx->streams[0]->time_base);

            // 写入一个编码的数据包到输出文件
            if (av_interleaved_write_frame(ofmtCtx, out_packet) < 0)
            {
                fprintf(stderr, "Error while writing output packet\n");
                break;
            }
            av_packet_unref(out_packet);
        }
    }
}


/**
 *
 *  把frame 转化一下，直接保存pcm数据，不用编码
 */
static void process_frame(AVPacket* pkt, uint8_t* in_buffer, uint8_t* out_buffer, struct SwrContext* swr_ctx, FILE* out_file, int out_buffer_size)
{
    //把packet中数据 拷贝到in_buffer中
    memcpy(in_buffer, pkt->data, pkt->size);
    //重采样音频数据
    int ret = swr_convert(swr_ctx, &out_buffer, 512, &in_buffer, 512);

    if (ret < 0)
    {
        char error[1024] = {0};
        av_strerror(ret, error, 1024);
        fprintf(stderr, "swr_convert error: %s\n", error);
        return;
    }


    fwrite(out_buffer, out_buffer_size, 1, out_file);
    fflush(out_file);
}


static void process_frame_use_decode(AVPacket* pkt, AVFrame* decoded_frame, AVFrame* swr_frame, AVAudioFifo* fifo,
                                     AVCodecContext* decoder_ctx, AVCodecContext* encoder_ctx,
                                     AVFormatContext* ofmtCtx, struct SwrContext* swr_ctx, AVPacket* out_packet, int* frameIndex)
{
    int ret = avcodec_send_packet(decoder_ctx, pkt);

    if (ret < 0)
    {
        char error[1024] = {0};
        av_strerror(ret, error, 1024);
        fprintf(stderr, "Decode error: %s\n", error);
        return;
    }

    int received_frame = 0;

    while (avcodec_receive_frame(decoder_ctx, decoded_frame) == 0)
    {
        received_frame++;

        // swr_frame->ch_layout = encoder_ctx->ch_layout;
        //
        // swr_frame->format = encoder_ctx->sample_fmt;
        // swr_frame->sample_rate = 48000;
        // swr_frame->nb_samples = encoder_ctx->frame_size; //与编码器帧大小保持一致


        ret = swr_convert_frame(swr_ctx, swr_frame, decoded_frame);

        // 打印pts，dts
        printf("decoded_frame pts: %ld, dts: %ld\n", decoded_frame->pts, decoded_frame->pkt_dts);
        printf("swr_frame pts: %ld, dts: %ld\n", swr_frame->pts, swr_frame->pkt_dts);

        if (ret < 0)
        {
            char error[1024] = {0};
            av_strerror(ret, error, 1024);
            fprintf(stderr, "Error while resampling: %s\n", error);
            return;
        }

        // Add resampled samples to FIFO
        ret = av_audio_fifo_write(fifo, (void**)swr_frame->data, swr_frame->nb_samples);
        if (ret < swr_frame->nb_samples)
        {
            fprintf(stderr, "Failed to write all samples to FIFO\n");
            return;
        }
        printf(" frame received this time num: %d, Added %d samples to FIFO, current size: %d\n", received_frame, swr_frame->nb_samples, av_audio_fifo_size(fifo));

        // Encode when enough samples are available
        while (av_audio_fifo_size(fifo) >= encoder_ctx->frame_size)
        {
            printf("FIFO size: %d, frame size: %d\n", av_audio_fifo_size(fifo), encoder_ctx->frame_size);
            swr_frame->nb_samples = encoder_ctx->frame_size;
            ret = av_audio_fifo_read(fifo, (void**)swr_frame->data, encoder_ctx->frame_size);
            if (ret < encoder_ctx->frame_size)
            {
                fprintf(stderr, "Failed to read enough samples from FIFO\n");
                return;
            }

            swr_frame->pts = *frameIndex * encoder_ctx->frame_size;
            printf("in fifo swr_frame pts: %ld, dts: %ld\n", swr_frame->pts, swr_frame->pkt_dts);

            encode_and_write_frame(encoder_ctx, swr_frame, ofmtCtx, out_packet, *frameIndex);
            (*frameIndex)++;
        }
    }
}


int record_audio()
{
    AVFormatContext* ofmt_ctx = NULL; // 输出格式上下文
    AVCodecContext* decoder_ctx = NULL; // 解码器上下文
    AVCodecContext* encodec_ctx = NULL; // 编码器上下文
    AVPacket* packet = av_packet_alloc();
    AVPacket* out_packet = av_packet_alloc();

    AVFrame* frame = av_frame_alloc();
    AVStream* out_stream = NULL; // 输出流
    AVAudioFifo* fifo = NULL;
    int audio_stream_index = -1;
    SwrContext* swr_ctx = swr_alloc();
    AVFrame* swr_frame;
    int frame_index = 0;
    int ret;


    AVFormatContext* ifmtCtx = open_device();

    // 1、打开设备并初始化解码器
    decoder_ctx = open_decoder_by_format_context(ifmtCtx, &audio_stream_index, AVMEDIA_TYPE_AUDIO);


    //  ------------begin ------------  编码音频帧，需要的
    // 2、打开音频编码器
    encodec_ctx = open_encoder_audio();

    // 3、创建输出上下文并初始化 流、写入头文件
    ofmt_ctx = init_output_format_context("/Users/xuan/CLionProjects/ffmpeg/1.aac", encodec_ctx);


    // 4、初始化重采样上下文
    swr_frame = create_writable_audio_frame(encodec_ctx->ch_layout, encodec_ctx->frame_size, encodec_ctx->sample_fmt);

    // 5、创建重采样上下文
    ret = initialize_resampler(&swr_ctx,
                               &encodec_ctx->ch_layout, encodec_ctx->sample_fmt, encodec_ctx->sample_rate,
                               &decoder_ctx->ch_layout, decoder_ctx->sample_fmt, decoder_ctx->sample_rate);

    if (ret != 0)
    {
        fprintf(stderr, "init resampler failed\n");
        return -1;
    }


    // Initialize FIFO
    fifo = av_audio_fifo_alloc(encodec_ctx->sample_fmt, encodec_ctx->ch_layout.nb_channels, 1024 * 10); // Buffer for 10 frames
    if (!fifo)
    {
        fprintf(stderr, "Failed to allocate FIFO\n");
        goto end;
    }
    //  ------------ end ------------ 编码音频帧，需要的

    int count = 0;

    // 解码 ---- 重采样  -----编码
    while (count < 500)
    {
        ret = av_read_frame(ifmtCtx, packet);

        // ret返回-35 表示设备还没准备好, 先睡眠1s
        // device not ready, sleep 1s
        if (ret == -35)
        {
            av_log(NULL, AV_LOG_WARNING, "device not ready, wait 0.5s\n");
            av_packet_unref(packet);
            usleep(5000);
            continue;
        }
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "read data error from device\n");
            av_packet_unref(packet);
            break;
        }


        if (packet->stream_index == audio_stream_index)
        {
            process_frame_use_decode(packet, frame, swr_frame, fifo, decoder_ctx, encodec_ctx, ofmt_ctx, swr_ctx, out_packet, &frame_index);
            // av_frame_unref(swr_frame); // 清理帧数据以便重用

            count++;
        }
        av_packet_unref(packet);
    }

    // Flush remaining samples in FIFO
    while (av_audio_fifo_size(fifo) > 0)
    {
        int samples_to_read = FFMIN(av_audio_fifo_size(fifo), encodec_ctx->frame_size);
        swr_frame->nb_samples = samples_to_read;
        ret = av_audio_fifo_read(fifo, (void**)swr_frame->data, samples_to_read);
        if (ret < samples_to_read)
        {
            fprintf(stderr, "Failed to read remaining samples from FIFO\n");
            break;
        }
        swr_frame->pts = frame_index * encodec_ctx->frame_size;
        encode_and_write_frame(encodec_ctx, swr_frame, ofmt_ctx, out_packet, frame_index);
        frame_index++;
    }

    // Flush encoder
    encode_and_write_frame(encodec_ctx, NULL, ofmt_ctx, out_packet, frame_index);

    // 写入文件尾部信息
    if (av_write_trailer(ofmt_ctx) < 0)
    {
        fprintf(stderr, "Error writing trailer of the output file\n");
    }

end:
    // 关闭输出文件和释放输出上下文
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
    {
        avio_closep(&ofmt_ctx->pb);
    }
    avformat_free_context(ofmt_ctx);
    av_packet_free(&packet);
    av_packet_free(&out_packet);
    av_frame_free(&frame);
    av_frame_free(&swr_frame);
    av_audio_fifo_free(fifo);
    avcodec_close(decoder_ctx);
    avcodec_close(encodec_ctx);
    avformat_close_input(&ifmtCtx);
    swr_free(&swr_ctx);

    return 0;
}


int record_audio2()
{
    AVCodecContext* decoder_ctx = NULL; // 解码器上下文
    AVPacket* packet = av_packet_alloc();

    int audio_stream_index = -1;
    SwrContext* swr_ctx = swr_alloc();
    int ret;


    AVFormatContext* ifmtCtx = open_device();
    if (!ifmtCtx) {
        fprintf(stderr, "Failed to open input device\n");
        goto end;
    }

    // 1、打开设备并初始化解码器
    decoder_ctx = open_decoder_by_format_context(ifmtCtx, &audio_stream_index, AVMEDIA_TYPE_AUDIO);
    if (!decoder_ctx) {
        fprintf(stderr, "Failed to initialize decoder\n");
        goto end;
    }

    const char* file_path = "/Users/xuan/CLionProjects/ffmpeg/1.pcm";
    // const char* file_path = "/Users/xuan/CLionProjects/ffmpeg/audio.aac";
    // 判断文件是否存在，如果存在，就删除
    remove(file_path);

    FILE* out_file = fopen(file_path, "wb+");
    if (!out_file) {
        fprintf(stderr, "Failed to open output file\n");
        goto end;
    }

    // Input buffer (based on maximum expected packet size)
    uint8_t* in_buffer = NULL;
    int in_buffer_size = 0;

    int in_nb_samples = 512; // Default assumption; adjust dynamically if needed
    //分配输入音频数据缓冲区
    //AV_SAMPLE_FMT_FLT 分配的in_buffer_size大小为4096
    //AV_SAMPLE_FMT_FLTP 分配的in_buffer_size大小为2048
    //带P（plane）的数据格式在存储时，其左声道和右声道的数据是分开存储的，左声道的数据存储在data0，右声道的数据存储在data1，av_samples_alloc 返回的linesize 就是单个声道的数据大小
    //不带P（packed）的⾳频数据在存储时，是按照LRLRLR...的格式交替存储在data0中，av_samples_alloc 返回的linesize 就是所有声道的数据大小
    av_samples_alloc(&in_buffer, &in_buffer_size, decoder_ctx->ch_layout.nb_channels, in_nb_samples, decoder_ctx->sample_fmt, 0);


    // Allocated in_buffer_size: 2048 for 512 samples
    printf("Allocated in_buffer_size: %d for %d samples ,sample_fmt = %d\n", in_buffer_size, in_nb_samples, decoder_ctx->sample_fmt);

    //创建输出音频数据缓冲区
    int nb_channels_output = 2;
    int out_nb_samples = 512;
    enum AVSampleFormat sample_fmt_output = AV_SAMPLE_FMT_S16;
    uint8_t* out_buffer;
    //输出的音频数据大小
    int out_buffer_size = 0;
    //分配输出音频数据缓冲区
    av_samples_alloc(&out_buffer, &out_buffer_size, nb_channels_output, out_nb_samples, sample_fmt_output, 0);

    printf("Allocated out_buffer_size: %d for %d samples ,sample_fmt = %d\n", out_buffer_size, out_nb_samples, sample_fmt_output);


    int sample_rate_output = 48000;
    AVChannelLayout ch_layout_output = AV_CHANNEL_LAYOUT_STEREO;

    // 5、创建重采样上下文
    ret = initialize_resampler(&swr_ctx, &ch_layout_output, sample_fmt_output, sample_rate_output,
                               &decoder_ctx->ch_layout, decoder_ctx->sample_fmt, decoder_ctx->sample_rate);

    if (ret != 0)
    {
        fprintf(stderr, "init resampler failed\n");
        return -1;
    }


    int count = 0;

    // 解码 ---- 重采样  ---- 保存pcm数据
    while (count < 500)
    {
        ret = av_read_frame(ifmtCtx, packet);

        // ret返回-35 表示设备还没准备好, 先睡眠1s
        // device not ready, sleep 1s
        if (ret == -35)
        {
            av_log(NULL, AV_LOG_WARNING, "device not ready, wait 0.5s\n");
            av_packet_unref(packet);
            usleep(5000);
            continue;
        }
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "read data error from device\n");
            av_packet_unref(packet);
            break;
        }


        if (packet->stream_index == audio_stream_index)
        {
            //单声道输出 pkt size is 2048(0x12580f400)
            //一帧音频包含1024个sample
            av_log(NULL, AV_LOG_INFO,
                   "pkt size is %d(%p),count=%d \n",
                   packet->size, packet->data, count);

            process_frame(packet, in_buffer, out_buffer, swr_ctx, out_file, out_buffer_size);
            // process_frame_use_decode(packet, frame, swr_frame, fifo, decoder_ctx, encodec_ctx, ofmt_ctx, swr_ctx, out_packet, &frame_index);
            // av_frame_unref(swr_frame); // 清理帧数据以便重用
            count++;
        }
        av_packet_unref(packet);
    }


end:

    av_packet_free(&packet);
    avcodec_close(decoder_ctx);
    avformat_close_input(&ifmtCtx);
    swr_free(&swr_ctx);

    return 0;
}
