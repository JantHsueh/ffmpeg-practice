cmake_minimum_required(VERSION 3.28)
project(ffmpeg C)

set(CMAKE_C_STANDARD 11)

# 以下路径是ffmpeg编译安装后的路径
set(FFMPEG_PATH /usr/local/ffmpeg)
include_directories(${FFMPEG_PATH}/include/) # 头文件搜索路径
link_directories(${FFMPEG_PATH}/lib/) # 动态链接库或静态链接库的搜索路径

add_executable(ffmpeg main.c)

target_link_libraries(
        ffmpeg
        avcodec
        avdevice
        avfilter
        avformat
        avutil
        swresample
        swscale
)
