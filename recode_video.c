#include <stdio.h>
 
#define __STDC_CONSTANT_MACROS

#include <unistd.h>

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "libavutil/time.h"
 
int main()
{
	AVFormatContext *ifmtCtx = NULL;
	AVFormatContext *ofmtCtx = NULL;
	AVPacket pkt;
	AVFrame *pFrame, *pFrameYUV;
	struct SwsContext *pImgConvertCtx;
	AVDictionary *params = NULL;
	AVCodec *av_codec_decoder_in;
	AVCodecContext *av_codec_ctx_in;
	unsigned char *outBuffer;
	AVCodecContext *pH264CodecCtx;
	AVCodec *pH264Codec;
	AVDictionary *options = NULL;

	int ret = 0;
	unsigned int i = 0;
	int videoIndex = -1;
	int frameIndex = 0;

	const char *inFilename = "0";//输入URL
	const char *outFilename = "/Users/xuan/CLionProjects/ffmpeg/output.mp4"; //输出URL
	const char *ofmtName = NULL;

	avdevice_register_all();
	avformat_network_init();

	AVInputFormat *ifmt = av_find_input_format("avfoundation");
	if (!ifmt)
	{
		printf("can't find input device\n");
		goto end;
	}

	// 1. 打开输入
	// 1.1 打开输入文件，获取封装格式相关信息
	ifmtCtx = avformat_alloc_context();
	if (!ifmtCtx)
	{
		printf("can't alloc AVFormatContext\n");
		goto end;
	}
	av_dict_set(&options, "video_size", "640x480", 0);
	av_dict_set(&options, "framerate", "30", 0);
	av_dict_set_int(&options, "rtbufsize", 18432000  , 0);
	//设置视频格式为NV12
	av_dict_set(&options, "pixel_format", "nv12", 0);
	if ((ret = avformat_open_input(&ifmtCtx, inFilename, ifmt, &options)) < 0)
	{
		printf("can't open input file: %s\n", inFilename);
		goto end;
	}

	// 1.2 解码一段数据，获取流相关信息
	if ((ret = avformat_find_stream_info(ifmtCtx, 0)) < 0)
	{
		printf("failed to retrieve input stream information\n");
		goto end;
	}

	// 1.3 获取输入ctx
	for (i=0; i<ifmtCtx->nb_streams; ++i)
	{
		if (ifmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoIndex = i;
			break;
		}
	}

	printf("%s:%d, videoIndex = %d\n", __FUNCTION__, __LINE__, videoIndex);

	av_dump_format(ifmtCtx, 0, inFilename, 0);

	// 1.4 查找输入解码器
	av_codec_decoder_in = avcodec_find_decoder(ifmtCtx->streams[videoIndex]->codecpar->codec_id);
	if (!av_codec_decoder_in)
	{
		printf("can't find codec\n");
		goto end;
	}

	av_codec_ctx_in = avcodec_alloc_context3(av_codec_decoder_in);
	if (!av_codec_ctx_in)
	{
		printf("can't alloc codec context\n");
		goto end;
	}

	avcodec_parameters_to_context(av_codec_ctx_in, ifmtCtx->streams[videoIndex]->codecpar);

	//  1.5 打开输入解码器
	if (avcodec_open2(av_codec_ctx_in, av_codec_decoder_in, NULL) < 0)
	{
		printf("can't open codec\n");
		goto end;
	}


	// 1.6 查找H264编码器
	pH264Codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!pH264Codec)
	{
		printf("can't find h264 codec.\n");
		goto end;
	}

	// 1.6.1 设置参数
	pH264CodecCtx = avcodec_alloc_context3(pH264Codec);
	pH264CodecCtx->codec_id = AV_CODEC_ID_H264;
	pH264CodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	pH264CodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	pH264CodecCtx->width = av_codec_ctx_in->width;
	pH264CodecCtx->height = av_codec_ctx_in->height;
	pH264CodecCtx->time_base.num = 1;
	pH264CodecCtx->time_base.den = 25;	//帧率（即一秒钟多少张图片）
	pH264CodecCtx->bit_rate = 400000;	//比特率（调节这个大小可以改变编码后视频的质量）
	pH264CodecCtx->gop_size = 250;
	pH264CodecCtx->qmin = 10;
	pH264CodecCtx->qmax = 51;
	//some formats want stream headers to be separate
//	if (pH264CodecCtx->flags & AVFMT_GLOBALHEADER)
	{
		pH264CodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}


	// 1.7 打开H.264编码器
	av_dict_set(&params, "preset", "superfast", 0);
	av_dict_set(&params, "tune", "zerolatency", 0);	//实现实时编码
	if (avcodec_open2(pH264CodecCtx, pH264Codec, &params) < 0)
	{
		printf("can't open video encoder.\n");
		goto end;
	}

	// 2. 打开输出
	// 2.1 分配输出ctx
	if (strstr(outFilename, "rtmp://"))
	{
		ofmtName = "flv";
	}
	else if (strstr(outFilename, "udp://"))
	{
		ofmtName = "mpegts";
	}
	else
	{
		ofmtName = NULL;
	}

	avformat_alloc_output_context2(&ofmtCtx, NULL, ofmtName, outFilename);
	if (!ofmtCtx)
	{
		printf("can't create output context\n");
		goto end;
	}

	// 2.2 创建输出流
	for (i=0; i<ifmtCtx->nb_streams; ++i)
	{
		AVStream *outStream = avformat_new_stream(ofmtCtx, NULL);
		if (!outStream)
		{
			printf("failed to allocate output stream\n");
			goto end;
		}

		avcodec_parameters_from_context(outStream->codecpar, pH264CodecCtx);
	}

	av_dump_format(ofmtCtx, 0, outFilename, 1);

	if (!(ofmtCtx->oformat->flags & AVFMT_NOFILE))
	{
		// 2.3 创建并初始化一个AVIOContext, 用以访问URL（outFilename）指定的资源
		ret = avio_open(&ofmtCtx->pb, outFilename, AVIO_FLAG_WRITE);
		if (ret < 0)
		{
			printf("can't open output URL: %s\n", outFilename);
			goto end;
		}
	}

	// 3. 数据处理
	// 3.1 写输出文件
	ret = avformat_write_header(ofmtCtx, NULL);
	if (ret < 0)
	{
		printf("Error accourred when opening output file\n");
		goto end;
	}


	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();

	outBuffer = (unsigned char*) av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, av_codec_ctx_in->width,av_codec_ctx_in->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, outBuffer,AV_PIX_FMT_YUV420P, av_codec_ctx_in->width, av_codec_ctx_in->height, 1);

	pImgConvertCtx = sws_getContext(av_codec_ctx_in->width, av_codec_ctx_in->height,
			av_codec_ctx_in->pix_fmt, av_codec_ctx_in->width, av_codec_ctx_in->height,
			AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


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
			ret = avcodec_send_packet(av_codec_ctx_in, &pkt);
			if (ret < 0)
			{
				printf("Decode error.\n");
				goto end;
			}

			if (avcodec_receive_frame(av_codec_ctx_in, pFrame) >= 0)
			{
				sws_scale(pImgConvertCtx,
						(const unsigned char* const*) pFrame->data,
						pFrame->linesize, 0, av_codec_ctx_in->height, pFrameYUV->data,
						pFrameYUV->linesize);


				pFrameYUV->format = AV_PIX_FMT_YUV420P;
				pFrameYUV->width = av_codec_ctx_in->width;
				pFrameYUV->height = av_codec_ctx_in->height;
 
				ret = avcodec_send_frame(pH264CodecCtx, pFrameYUV);
				if (ret < 0)
				{
					printf("failed to encode.\n");
					goto end;
				}
 
				if (avcodec_receive_packet(pH264CodecCtx, &pkt) >= 0)
				{
					// 设置输出DTS,PTS
			        pkt.pts = pkt.dts = frameIndex * (ofmtCtx->streams[0]->time_base.den) /ofmtCtx->streams[0]->time_base.num / 25;
			        frameIndex++;
 
					ret = av_interleaved_write_frame(ofmtCtx, &pkt);
					if (ret < 0)
					{
						printf("send packet failed: %d\n", ret);
					}
					else
					{
						printf("send %5d packet successfully!\n", frameIndex);
					}
				}
			}
		}
        av_frame_unref(pFrame);
		av_packet_unref(&pkt);
	}
 
    av_write_trailer(ofmtCtx);
 
end:
    avformat_close_input(&ifmtCtx);
 
    /* close output */
    if (ofmtCtx && !(ofmtCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&ofmtCtx->pb);
    }
    avformat_free_context(ofmtCtx);
 
    if (ret < 0 && ret != AVERROR_EOF) {
        printf("Error occurred\n");
        return -1;
    }
 
    return 0;
}