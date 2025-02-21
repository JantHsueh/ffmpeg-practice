cmake_minimum_required(VERSION 3.28)
project(ffmpeg C)

set(CMAKE_C_STANDARD 11)

# Enable verbose linking
set(CMAKE_VERBOSE_MAKEFILE ON)

# 以下路径是ffmpeg编译安装后的路径
set(FFMPEG_PATH /usr/local/ffmpeg)
include_directories(${FFMPEG_PATH}/include/) # 头文件搜索路径
include_directories(/opt/homebrew/Cellar/rtmpdump/2.4-20151223_3/include/) # 头文件搜索路径
include_directories(/opt/homebrew/Cellar/openssl@3/3.4.0/include/) # 头文件搜索路径
# libz. tbd
link_directories(${FFMPEG_PATH}/lib/) # 动态链接库或静态链接库的搜索路径
link_directories(/opt/homebrew/Cellar/rtmpdump/2.4-20151223_3/lib/) # 动态链接库或静态链接库的搜索路径
link_directories(/opt/homebrew/Cellar/openssl@3/3.4.0/lib/) # 动态链接库或静态链接库的搜索路径
#link_directories(/opt/homebrew/Cellar/openssl@3/3.4.0/lib/libssl.dylib) # 动态链接库或静态链接库的搜索路径

add_executable(ffmpeg main.c
        acc.c
        pushstream.c
        record_video.c
        video_audio.h
        av_common.c)

target_link_libraries(
        ffmpeg
        avcodec
        avdevice
        avfilter
        avformat
        avutil
        swresample
        swscale
        /opt/homebrew/Cellar/rtmpdump/2.4-20151223_3/lib/librtmp.dylib
)
