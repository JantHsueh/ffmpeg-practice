#include <stdio.h>

#define __STDC_CONSTANT_MACROS

#include <unistd.h>

#include "av_common.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"

static int64_t first_pts = AV_NOPTS_VALUE;

static void encode_and_write_frame(AVCodecContext* h264_encoder_ctx, AVFrame* converted_frame, AVFormatContext* ofmtCtx, AVPacket* pkt, int frameIndex)
{
    int ret = avcodec_send_frame(h264_encoder_ctx, converted_frame);
    if (ret < 0)
    {
        printf("failed to encode.\n");
        return;
    }

    ret = avcodec_receive_packet(h264_encoder_ctx, pkt);
    if (ret < 0)
    {
        printf("avcodec_receive_packet failed to encode.\n");
        return;
    }
    // 设置输出DTS,PTS，如果启用b帧，pts 和 dts 不能设置相同值，会出现视频画面前面跳动
    // pkt->pts = pkt->dts = frameIndex * (ofmtCtx->streams[0]->time_base.den) / ofmtCtx->streams[0]->time_base.num / 25;


    // 使用编码器提供的 PTS/DTS 并转换到输出流的时间基
    // pkt->pts = av_rescale_q_rnd(pkt->pts, h264_encoder_ctx->time_base, ofmtCtx->streams[0]->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    // pkt->dts = av_rescale_q_rnd(pkt->dts, h264_encoder_ctx->time_base, ofmtCtx->streams[0]->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    // pkt->duration = av_rescale_q(1, h264_encoder_ctx->time_base, ofmtCtx->streams[0]->time_base);
    av_packet_rescale_ts(pkt, h264_encoder_ctx->time_base, ofmtCtx->streams[0]->time_base);

    pkt->stream_index = 0; // 确保设置 stream_index


    // 打印pkt 中的pts，dts，duration
    printf("pkt pts: %ld, dts: %ld, duration: %ld\n", pkt->pts, pkt->dts, pkt->duration);


    ret = av_interleaved_write_frame(ofmtCtx, pkt);
    if (ret < 0)
    {
        printf("send packet failed: %d\n", ret);
    }
    else
    {
        printf("send %5d packet successfully!\n\n", frameIndex);
    }
}


static void process_frame(AVPacket* pkt, AVFrame* converted_frame, AVCodecContext* h264_encoder_ctx,
                          AVFormatContext* ofmtCtx, int frameIndex)
{
    // 把NV12 数据转为YUV420
    memcpy(converted_frame->data[0], pkt->data, 307200);

    for (int i = 0; i < 307200 / 4; i++)
    {
        converted_frame->data[1][i] = pkt->data[307200 + i * 2];
        converted_frame->data[2][i] = pkt->data[307200 + i * 2 + 1];
    }

    // 调试日志
    av_log(NULL, AV_LOG_INFO, "Frame count: %d, PTS: %ld\n",
           frameIndex, converted_frame->pts, converted_frame->pkt_dts);
    // 需要加上这行代码，否则报non-strictly-monotonic PTS
    converted_frame->pts = frameIndex;
    encode_and_write_frame(h264_encoder_ctx, converted_frame, ofmtCtx, pkt, frameIndex);
}


static void process_frame_use_decode(AVPacket* pkt, AVFrame* decoded_frame, AVFrame* converted_frame, AVCodecContext* av_decoder_ctx, AVCodecContext* h264_encoder_ctx,
                                     AVFormatContext* ofmtCtx, struct SwsContext* sws_ctx, int frameIndex)
{
    //av_read_frame 接收到的是原始数据，也可以解码后再发送给编码器。如果接收到是编码后的数据packet，需要解码后再发送给编码器
    int ret = avcodec_send_packet(av_decoder_ctx, pkt);
    if (ret < 0)
    {
        printf("Decode error.\n");
        return;
    }

    if (avcodec_receive_frame(av_decoder_ctx, decoded_frame) >= 0)
    {
        sws_scale(sws_ctx,
                  decoded_frame->data, decoded_frame->linesize, 0, av_decoder_ctx->height,
                  converted_frame->data, converted_frame->linesize);



        // Store the first PTS value
        if (first_pts == AV_NOPTS_VALUE)
        {
            first_pts = decoded_frame->pts;
        }


        //打印av_decoder_ctx->time_base、 h264_encoder_ctx->time_base
        printf("av_decoder_ctx time_base: %d/%d\n", av_decoder_ctx->time_base.num, av_decoder_ctx->time_base.den);
        printf("h264_encoder_ctx time_base: %d/%d\n", h264_encoder_ctx->time_base.num, h264_encoder_ctx->time_base.den);

        // 将 decoded_frame 的 PTS 传递给 converted_frame, 并进行时间基转换
        converted_frame->pts = decoded_frame->pts - first_pts;

        // 将 decoded_frame 的 PTS 和 DTS 进行时间基转换，传递给 converted_frame
        converted_frame->pts = av_rescale_q(converted_frame->pts, av_decoder_ctx->time_base, h264_encoder_ctx->time_base);

        // 因为此时是未编码的帧，所以dts和pts相同，经过h264编码后，dts会变化
        converted_frame->pkt_dts = converted_frame->pts;
        // 打印pts，dts
        printf("decoded_frame pts: %ld, dts: %ld\n", decoded_frame->pts, decoded_frame->pkt_dts);
        printf("converted_frame pts: %ld, dts: %ld\n", converted_frame->pts, converted_frame->pkt_dts);

        encode_and_write_frame(h264_encoder_ctx, converted_frame, ofmtCtx, pkt, frameIndex);
    }
}


void record_video()
{
    AVFormatContext* ifmtCtx = NULL;
    AVFormatContext* ofmtCtx = NULL;
    AVPacket pkt;
    AVFrame *decoded_frame, *converted_frame;
    struct SwsContext* sws_ctx;
    AVCodecContext *av_decoder_ctx, *h264_encoder_ctx;


    int ret = 0;
    int videoIndex = -1;
    int frameIndex = 0;

    avdevice_register_all();
    // avformat_network_init();


    // 1. 打开输入设备
    ifmtCtx = open_device();

    // 1.2 解码一段数据，获取流相关信息
    if ((ret = avformat_find_stream_info(ifmtCtx, 0)) < 0)
    {
        printf("failed to retrieve input stream information\n");
        goto end;
    }

    // 2、 打开输入流对应的解码器
    av_decoder_ctx = open_decoder_by_format_context(ifmtCtx, &videoIndex);

    // 3、 打开H264编码器
    h264_encoder_ctx = open_h264_encoder(av_decoder_ctx->width, av_decoder_ctx->height);

    // 4、创建输出上下文并初始化 流、写入头文件
    ofmtCtx = init_output_format_context("/Users/xuan/CLionProjects/ffmpeg/2.mp4", h264_encoder_ctx);

    decoded_frame = av_frame_alloc();
    converted_frame = create_writable_frame(h264_encoder_ctx->width, h264_encoder_ctx->height, AV_PIX_FMT_YUV420P);


    sws_ctx = sws_getContext(av_decoder_ctx->width, av_decoder_ctx->height, av_decoder_ctx->pix_fmt,
                             h264_encoder_ctx->width, h264_encoder_ctx->height, AV_PIX_FMT_YUV420P,
                             SWS_BICUBIC, NULL, NULL, NULL);

    // 1、av_read_frame收到原始数据
    // 2、用输入流对应的解码器解码，
    // 3、经过转换器转换成YUV420P格式
    // 4、用h264对应的编码器编码
    while (frameIndex < 200)
    {
        // 3.2 从输入流读取一个packet
        ret = av_read_frame(ifmtCtx, &pkt);

        // ret返回-35 表示设备还没准备好, 先睡眠1s
        // device not ready, sleep 1s
        if (ret == -35)
        {
            // av_log(NULL, AV_LOG_WARNING, "device not ready, wait 0.5s\n");
            av_packet_unref(&pkt);
            usleep(5000);
            continue;
        }
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "read data error from device\n");
            av_packet_unref(&pkt);
            break;
        }


        if (pkt.stream_index == videoIndex)
        {
            // av_log(NULL, AV_LOG_INFO,
            //        "pkt size is %d(%p), pts= %ld, dts=%ld, count=%d",
            //        pkt.size, pkt.data, pkt.pts, pkt.dts, frameIndex);


            // process frame 多选一

            //av_read_frame 接收到的是原始数据，不需要解码，可以这样直接发送给编码器
            process_frame(&pkt, converted_frame, h264_encoder_ctx, ofmtCtx,frameIndex);

            //av_read_frame 接收到的是原始数据，也可以解码后再发送给编码器。如果接收到是编码后的数据packet，需要解码后再发送给编码器
            // process_frame_use_decode(&pkt, decoded_frame, converted_frame, av_decoder_ctx, h264_encoder_ctx, ofmtCtx, sws_ctx, frameIndex);
            frameIndex++;
        }
        av_frame_unref(decoded_frame);
        av_packet_unref(&pkt);
    }

    av_write_trailer(ofmtCtx);

end:
    avformat_close_input(&ifmtCtx);

    /* close output */
    if (ofmtCtx && !(ofmtCtx->oformat->flags & AVFMT_NOFILE))
    {
        avio_closep(&ofmtCtx->pb);
    }
    avformat_free_context(ofmtCtx);

    if (ret < 0 && ret != AVERROR_EOF)
    {
        printf("Error occurred\n");
        return;
    }
}
