#include <iostream>
#include <string>
#include <windows.h>
#include <filesystem>

extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/error.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
#include "libavcodec/avcodec.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include <libavutil/rational.h>

}

int main() {
    avformat_network_init();

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    // Extract the directory path from the executable path
    std::string exeDirectory(exePath);
    size_t pos = exeDirectory.find_last_of("\\/");
    std::string directory = exeDirectory.substr(0, pos);

    // Create the full path of the "video.mp4" file
    std::string videoPath = directory + "\\480i_conv.ts";

    const char* fileName = videoPath.c_str();

    // Open input file
    AVFormatContext* inputFormatContext = nullptr;
    if (avformat_open_input(&inputFormatContext, fileName, nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file" << std::endl;
        return -1;
    }
    av_dump_format(inputFormatContext, 0, NULL, 0);
    // Retrieve stream information
    if (avformat_find_stream_info(inputFormatContext, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        return -1;
    }

    // Find video stream
    int videoStreamIndex = -1;
    AVCodecParameters* videoCodecParameters = nullptr;
    for (int i = 0; i < inputFormatContext->nb_streams; i++) {
        if (inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            videoCodecParameters = inputFormatContext->streams[i]->codecpar;
            break;
        }
    }

    if (videoStreamIndex == -1) {
        std::cerr << "Could not find video stream" << std::endl;
        return -1;
    }

    // Find video decoder
    AVCodec* videoCodec = avcodec_find_decoder(videoCodecParameters->codec_id);
    if (videoCodec == nullptr) {
        std::cerr << "Unsupported codec" << std::endl;
        return -1;
    }

    // Create codec context and open decoder
    AVCodecContext* videoCodecContext = avcodec_alloc_context3(videoCodec);
    if (avcodec_parameters_to_context(videoCodecContext, videoCodecParameters) < 0) {
        std::cerr << "Failed to copy codec parameters to decoder context" << std::endl;
        return -1;
    }

    if (avcodec_open2(videoCodecContext, videoCodec, nullptr) < 0) {
        std::cerr << "Failed to open video decoder" << std::endl;
        return -1;
    }
    //videoCodecContext = inputFormatContext->streams[videoStreamIndex]->codecpar;

    // Open output file
    AVFormatContext* outputFormatContext = nullptr;
    if (avformat_alloc_output_context2(&outputFormatContext, nullptr, nullptr,"EncodedVideo.mp4") < 0) {
        std::cerr << "Could not create output file" << std::endl;
        return -1;
    }

    // Add video stream to output file
    AVStream* outputVideoStream = avformat_new_stream(outputFormatContext, nullptr);
    if (outputVideoStream == nullptr) {
        std::cerr << "Failed to create video stream" << std::endl;
        return -1;
    }

    // Copy codec parameters to output video stream
    if (avcodec_parameters_copy(outputVideoStream->codecpar, videoCodecParameters) < 0) {
        std::cerr << "Failed to copy codec parameters to output video stream" << std::endl;
        return -1;
    }

    // Find video encoder
    AVCodec* videoEncoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (videoEncoder == nullptr) {
        std::cerr << "Unsupported encoder" << std::endl;
        return -1;
    }

    // Create codec context and open encoder
    AVCodecContext* videoEncoderContext = avcodec_alloc_context3(videoEncoder);
    if (avcodec_parameters_to_context(videoEncoderContext, outputVideoStream->codecpar) < 0) {
        std::cerr << "Failed to copy codec parameters to encoder context" << std::endl;
        return -1;
    }


    // Set encoder parameters
    videoEncoderContext->width = videoCodecContext->width;
    videoEncoderContext->height = videoCodecContext->height;
    // Set the frame rate
    //videoEncoderContext->framerate = av_guess_frame_rate(outputFormatContext, videoCodecContext->stream, videoCodecContext->frame_rate);

  
   /* videoEncoderContext->framerate = videoCodecContext->framerate;*/
    videoEncoderContext->framerate = inputFormatContext->streams[videoStreamIndex]->r_frame_rate;
    videoEncoderContext->time_base = inputFormatContext->streams[videoStreamIndex]->time_base; //{ 1, videoCodecContext->framerate.den }; //av_make_q(1, 15);
    videoEncoderContext->gop_size = videoCodecContext->gop_size;
    videoEncoderContext->pix_fmt = AV_PIX_FMT_YUV420P;

    if (avcodec_open2(videoEncoderContext, videoEncoder, nullptr) < 0) {
        std::cerr << "Failed to open video encoder" << std::endl;
        return -1;
    }

    // Open output file for writing
    if (avio_open(&outputFormatContext->pb, "EncodedVideo.mp4", AVIO_FLAG_WRITE) < 0) {
        std::cerr << "Could not open output file" << std::endl;
        return -1;
    }

    // Write output file header
    if (avformat_write_header(outputFormatContext, nullptr) < 0) {
        std::cerr << "Error occurred when writing output file header" << std::endl;
        return -1;
    }

    // Read frames from input file, decode, encode, and write to output file
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (av_read_frame(inputFormatContext, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            // Decode video packet
            if (avcodec_send_packet(videoCodecContext, packet) >= 0) {
                while (avcodec_receive_frame(videoCodecContext, frame) >= 0) {
                    // Encode video frame
                    AVPacket* encodedPacket = av_packet_alloc();
                    if (avcodec_send_frame(videoEncoderContext, frame) >= 0) {
                        while (avcodec_receive_packet(videoEncoderContext, encodedPacket) >= 0) {
                            // Set stream index and timebase for the encoded packet
                            encodedPacket->stream_index = outputVideoStream->index;
                            encodedPacket->pts = av_rescale_q(encodedPacket->pts, videoEncoderContext->time_base, outputVideoStream->time_base);
                            encodedPacket->dts = av_rescale_q(encodedPacket->dts, videoEncoderContext->time_base, outputVideoStream->time_base);
                            encodedPacket->duration = av_rescale_q(encodedPacket->duration, videoEncoderContext->time_base, outputVideoStream->time_base);

                            // Write encoded packet to output file
                            av_interleaved_write_frame(outputFormatContext, encodedPacket);
                        }
                    }
                    av_packet_unref(encodedPacket);
                }
            }
        }

        av_packet_unref(packet);
    }

    
    // Write output file trailer
    av_write_trailer(outputFormatContext);

    // Cleanup and release resources
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&videoEncoderContext);
    avcodec_free_context(&videoCodecContext);
    avformat_close_input(&inputFormatContext);
    avformat_free_context(outputFormatContext);

    return 0;
}