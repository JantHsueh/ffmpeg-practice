#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

// Helper function to convert pict_type to a character
char get_frame_type_char(enum AVPictureType pict_type)
{
    switch (pict_type)
    {
    case AV_PICTURE_TYPE_I: return 'I'; // Intra frame
    case AV_PICTURE_TYPE_P: return 'P'; // Predictive frame
    case AV_PICTURE_TYPE_B: return 'B'; // Bidirectional frame
    case AV_PICTURE_TYPE_S: return 'S'; // S-frame (MPEG-4)
    case AV_PICTURE_TYPE_SI: return 'i'; // SI-frame
    case AV_PICTURE_TYPE_SP: return 'p'; // SP-frame
    case AV_PICTURE_TYPE_BI: return 'b'; // BI-frame
    default: return '?'; // Unknown type
    }
}

int analyze_video()
{
    // if (argc < 2) {
    //     fprintf(stderr, "Usage: %s <input_video_file>\n", argv[0]);
    //     return 1;
    // }

    const char* input_file = "/Users/xuan/CLionProjects/ffmpeg/2.mp4";

    // Initialize FFmpeg (required in older versions)
    // av_register_all();

    // Open input file
    AVFormatContext* fmt_ctx = NULL;
    if (avformat_open_input(&fmt_ctx, input_file, NULL, NULL) < 0)
    {
        fprintf(stderr, "Could not open input file: %s\n", input_file);
        return 1;
    }
    printf("open input ok! {%s} video index = 0\n", input_file);

    // Find stream info
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0)
    {
        fprintf(stderr, "Could not find stream info\n");
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    // Find video stream
    int video_stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++)
    {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream_idx = i;
            break;
        }
    }
    if (video_stream_idx == -1)
    {
        fprintf(stderr, "Could not find video stream\n");
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    // Find decoder
    AVCodec* codec = avcodec_find_decoder(fmt_ctx->streams[video_stream_idx]->codecpar->codec_id);
    if (!codec)
    {
        fprintf(stderr, "Could not find decoder\n");
        avformat_close_input(&fmt_ctx);
        return 1;
    }
    printf("openDecode for stream type: video(%d), format: %s(%d)\n",
           video_stream_idx, codec->name, fmt_ctx->streams[video_stream_idx]->codecpar->codec_id);

    // Allocate codec context
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx)
    {
        fprintf(stderr, "Could not allocate codec context\n");
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    // Copy codec parameters
    if (avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_stream_idx]->codecpar) < 0)
    {
        fprintf(stderr, "Could not copy codec parameters\n");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    // Open decoder
    if (avcodec_open2(codec_ctx, codec, NULL) < 0)
    {
        fprintf(stderr, "Could not open decoder\n");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return 1;
    }
    printf("decoder is ready\n");

    // Allocate packet and frame
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!packet || !frame)
    {
        fprintf(stderr, "Could not allocate packet or frame\n");
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return 1;
    }

    int packet_count = 0;
    int frame_count = 0;

    // Read and process packets
    while (av_read_frame(fmt_ctx, packet) >= 0)
    {
        if (packet->stream_index == video_stream_idx)
        {
            // Determine packet type based on key flag
            char packet_type = (packet->flags & AV_PKT_FLAG_KEY) ? 'I' : '?';

            // Print packet info with type

            printf("\033[0;32m[%d] packet: <dts=%lld, pts=%lld, key: %d, size=%d, type='%c'>\033[0m\n",
                   packet_count++, packet->dts, packet->pts,
                   packet->flags & AV_PKT_FLAG_KEY, packet->size, packet_type);

            // Send packet to decoder
            int ret = avcodec_send_packet(codec_ctx, packet);
            if (ret < 0)
            {
                fprintf(stderr, "Error sending packet to decoder: %d\n", ret);
                break;
            }

            // Receive decoded frames
            while (ret >= 0)
            {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                else if (ret < 0)
                {
                    fprintf(stderr, "Error receiving frame: %d\n", ret);
                    break;
                }

                // Get frame type
                char frame_type = get_frame_type_char(frame->pict_type);

                // Print frame info with type
                printf("[%d] frame: <pkt_dts=%lld, pts=%lld, key:%d, type='%c'>\n",
                       frame_count++, frame->pkt_dts, frame->pts,
                       frame->key_frame, frame_type);
            }
        }
        av_packet_unref(packet);
    }
    printf("av_read_frame: End of file\n");

    // Draining mode for remaining frames
    printf("draining mode\n");
    avcodec_send_packet(codec_ctx, NULL); // Enter draining mode
    while (1)
    {
        int ret = avcodec_receive_frame(codec_ctx, frame);
        if (ret == AVERROR_EOF)
        {
            break;
        }
        else if (ret < 0)
        {
            printf("avcodec_receive_frame: End of file (%d)\n", ret);
            break;
        }

        // Get frame type
        char frame_type = get_frame_type_char(frame->pict_type);

        // Print drained frame info with type
        printf("[%d] frame: <pkt_dts=%lld, pts=%lld, key:%d, type='%c'>\n",
               frame_count++, frame->pkt_dts, frame->pts,
               frame->key_frame, frame_type);
    }
    printf("draining over\n");

    // Print totals
    printf("total: %d %d\n", packet_count, frame_count);

    // Clean up
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    printf("run out\n");
    return 0;
}
