//
// Created by xuan on 2024/11/22.
//


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "librtmp/rtmp.h"


/**
 * flv 文件头，共9个字节
 * 1-3  signature: "FLV"
 * 4    version: 1
 * 5    0-5位 保留，必须是0
 * 5 第6位 1表示有音频
 * 5 第7位 保留，必须是0
 * 5 第8位 1表示有视频
 * 6-9  dataoffset: 9
 *
 */
FILE* open_flv(char* flvaddr)
{
    FILE* fp = NULL;

    fp = fopen(flvaddr, "rb");
    if (fp == NULL)
    {
        printf("open file %s failed\n", flvaddr);
        return NULL;
    }

    //跳过前9个字节flv header
    fseek(fp, 9, SEEK_SET);
    // // 跳过4字节PreTabsize
    // fseek(fp, 4, SEEK_CUR);

    return fp;
}


RTMP* conect_rtmp_server(char* rtmpaddr)
{
    RTMP* rtmp = NULL;

    //1、创建rtmp对象
    rtmp = RTMP_Alloc();
    if (rtmp == NULL)
    {
        printf("RTMP_Alloc failed\n");
        return NULL;
    }

    //2、初始化rtmp对象
    RTMP_Init(rtmp);

    //3、设置rtmp url

    //设置超时时间
    rtmp->Link.timeout = 10;

    if (!RTMP_SetupURL(rtmp, rtmpaddr))
    {
        printf("RTMP_SetupURL failed\n");
        return NULL;
    }

    //4、如果设置了该开关就是推流（publish），如果位设置就是拉流（play）
    RTMP_EnableWrite(rtmp);

    //5、连接rtmp服务器
    if (!RTMP_Connect(rtmp, NULL))
    {
        printf("RTMP_Connect failed\n");
        return NULL;
    }


    //6、连接流
    if (!RTMP_ConnectStream(rtmp, 0))
    {
        printf("RTMP_ConnectStream failed\n");
        return NULL;
    }

    return rtmp;
}


void send_data(FILE* fp, RTMP* rtmp)
{
    //1、创建一个RTMPPacket对象
    RTMPPacket* packet = (RTMPPacket*)malloc(sizeof(RTMPPacket));

    RTMPPacket_Alloc(packet, 1024 * 64);
    RTMPPacket_Reset(packet);


    //表示该包没有绝对时间戳
    packet->m_hasAbsTimestamp = 0;
    //该包的通道号为 4。
    packet->m_nChannel = 0x04;


    // unsigned char previoustagsize[4] = {0};
    // fread(previoustagsize, 1, 4, fp);
    //
    // //2.2 读取tag header
    // // tag header 11个字节
    // // 第1字节 表示tag类型 8表示音频，9表示视频，18表示script data
    // // 第2-4字节 表示tag data的长度
    // // 第5-7字节 表示时间戳, 3个字节, 低位在前, 高位在后, 单位是毫秒
    // // 第8字节 表示时间戳扩展
    // // 第9-11字节 表示stream id, 3个字节, 0
    // unsigned char tagheader[11] = {0};
    // fread(tagheader, 1, 11, fp);
    //
    // //2.3 读取tag data
    // unsigned int datasize = tagheader[1] << 16 | tagheader[2] << 8 | tagheader[3];
    // unsigned char* tagdata = (unsigned char*)malloc(datasize);
    // unsigned int timestamp = tagheader[4] | tagheader[5] << 8 | tagheader[6] << 16;
    // fread(tagdata, 1, datasize, fp);
    // printf("timestamp: %d, datasize: %d, tagheader[0]: %d\n", timestamp, datasize, tagheader[0]);
    unsigned int pre_ts = 0;

    while (1)
    {
        //2、读取flv文件中的数据
        //2.1 读取前4个字节的previoustagsize
        unsigned char previoustagsize[4] = {0};
        fread(previoustagsize, 1, 4, fp);

        //2.2 读取tag header
        // tag header 11个字节
        // 第1字节 表示tag类型 8表示音频，9表示视频，18表示script data
        // 第2-4字节 表示tag data的长度
        // 第5-7字节 表示时间戳, 3个字节, 低位在前, 高位在后, 单位是毫秒
        // 第8字节 表示时间戳扩展
        // 第9-11字节 表示stream id, 3个字节, 0
        unsigned char tagheader[11] = {0};
        fread(tagheader, 1, 11, fp);

        //2.3 读取tag data
        unsigned int datasize = tagheader[1] << 16 | tagheader[2] << 8 | tagheader[3];
        unsigned char* tagdata = (unsigned char*)malloc(datasize);
        unsigned int timestamp = tagheader[6] | tagheader[5] << 8 | tagheader[4] << 16;
        fread(tagdata, 1, datasize, fp);


        // 打印timestamp、datasize、tagheader[0]
        printf("timestamp: %d, datasize: %d, tagheader[0]: %d\n", timestamp, datasize, tagheader[0]);

        //3、填充RTMPPacket
        packet->m_body = tagdata;
        packet->m_nBodySize = datasize;
        packet->m_packetType = tagheader[0];
        packet->m_headerType = RTMP_PACKET_SIZE_LARGE;
        packet->m_nTimeStamp = timestamp;
        // packet->m_nInfoField2 = rtmp->m_stream_id;


        // 根据当前包和下一个包的时间间隔，睡眠一段时间


        unsigned int diff = packet->m_nTimeStamp - pre_ts;

        usleep(diff * 1000);

        //4、发送数据
        if (!RTMP_SendPacket(rtmp, packet, 0))
        {
            printf("RTMP_SendPacket failed\n");
            break;
        }


        pre_ts = packet->m_nTimeStamp;

        free(tagdata);
    }

    //5、释放资源
    free(packet);
    RTMP_Close(rtmp);
    RTMP_Free(rtmp);
    fclose(fp);
}


void main()
{
    char* flvaddr = "/Users/xuan/1.flv";
    char* rtmpaddr = "rtmp://localhost/live/show/";


    //1、读flv 文件
    FILE* flv_file = open_flv(flvaddr);


    // 2、 连接rtmp服务器
    RTMP* rtmp = conect_rtmp_server(rtmpaddr);


    // 3、发送数据
    send_data(flv_file, rtmp);
}
