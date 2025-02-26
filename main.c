#include <stdio.h>
#include <time.h>

#include "libavutil/avutil.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include  "libswresample/swresample.h"
#include <unistd.h>
#include <sys/time.h>

#include "av_common.h"
#include "video_audio.h"



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
static int select_channel_layout(const AVCodec* codec, AVChannelLayout* dst)
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
    // record_video();
    analyze_video();
    // push_stream();
    // resample_audio();
    // record_audio();
    // 播放命令ffplay -ar 44100 -ch_layout stereo -f s16le 1.pcm
    // record_audio2();
    return 0;
}
