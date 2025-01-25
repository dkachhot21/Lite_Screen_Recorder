#include "ScreenRecorder.h"
#include <iostream>
#include <chrono>
#include <thread>

ScreenRecorder::ScreenRecorder()
    : d3d(nullptr), device(nullptr), surface(nullptr),
      fmt_ctx(nullptr), video_stream(nullptr), codec_ctx(nullptr), sws_ctx(nullptr),
      frame_width(0), frame_height(0), is_capturing(false) {}

ScreenRecorder::~ScreenRecorder()
{
    Stop();
}

std::pair<int, int> ScreenRecorder::GetResolution(Resolution resolution)
{
    switch (resolution)
    {
    case UHD_4K:
        return {3840, 2160};
    case FHD:
        return {1920, 1080};
    case HD:
        return {1280, 720};
    case SD:
        return {854, 480};
    default:
        return {1920, 1080};
    }
}

bool ScreenRecorder::Initialize(const std::string &outputFile, Resolution resolution)
{
    auto [width, height] = GetResolution(resolution);
    frame_width = width;
    frame_height = height;

    start_time = std::chrono::steady_clock::now();
    return InitDirect3D() && InitFFmpeg(outputFile);
}

bool ScreenRecorder::InitDirect3D()
{
    d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d)
        return false;

    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = GetDesktopWindow();

    if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, GetDesktopWindow(),
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &device)))
    {
        return false;
    }

    return SUCCEEDED(device->CreateOffscreenPlainSurface(
        frame_width, frame_height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &surface, nullptr));
}

bool ScreenRecorder::InitFFmpeg(const std::string &outputFile)
{
    avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, outputFile.c_str());
    if (!fmt_ctx)
        return false;

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
        return false;

    video_stream = avformat_new_stream(fmt_ctx, codec);
    if (!video_stream)
        return false;

    codec_ctx = avcodec_alloc_context3(codec);
    codec_ctx->width = frame_width;
    codec_ctx->height = frame_height;
    codec_ctx->time_base = {1, 60}; // 60 FPS
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx->bit_rate = 4000000; // 4 Mbps

    // Optimize for speed
    AVDictionary *options = nullptr;
    av_dict_set(&options, "preset", "ultrafast", 0);
    av_dict_set(&options, "tune", "fastdecode", 0); // Optimize for fast decoding
    av_dict_set(&options, "crf", "28", 0);          // Higher CRF for faster encoding (lower quality)

    if (avcodec_open2(codec_ctx, codec, &options) < 0)
    {
        av_dict_free(&options);
        return false;
    }
    av_dict_free(&options);

    video_stream->time_base = codec_ctx->time_base;
    avcodec_parameters_from_context(video_stream->codecpar, codec_ctx);

    if (avio_open(&fmt_ctx->pb, outputFile.c_str(), AVIO_FLAG_WRITE) < 0)
        return false;
    if (avformat_write_header(fmt_ctx, nullptr) < 0)
        return false;

    sws_ctx = sws_getContext(
        frame_width, frame_height, AV_PIX_FMT_BGRA,
        frame_width, frame_height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    return sws_ctx != nullptr;
}


void ScreenRecorder::Start()
{
    is_capturing = true;
    capture_thread = std::thread(&ScreenRecorder::CaptureLoop, this);
    encode_thread = std::thread(&ScreenRecorder::EncodeLoop, this);
}

void ScreenRecorder::CaptureLoop()
{
    constexpr int64_t target_frame_duration = 1000 / 60; // 60 FPS in milliseconds
    auto next_frame_time = std::chrono::steady_clock::now();

    while (is_capturing)
    {
        auto start_time = std::chrono::steady_clock::now();

        // Capture frame
        D3DLOCKED_RECT lockedRect;
        if (FAILED(device->GetFrontBufferData(0, surface)) ||
            FAILED(surface->LockRect(&lockedRect, nullptr, D3DLOCK_READONLY)))
        {
            continue;
        }

        std::vector<uint8_t> frame(frame_width * frame_height * 4);
        memcpy(frame.data(), lockedRect.pBits, frame.size());
        surface->UnlockRect();

        // Add frame to queue (drop old frames if queue is full)
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (frame_queue.size() >= 60)
            { // Keep up to 1 second of frames (60 FPS * 1 sec)
                frame_queue.pop();
            }
            frame_queue.push(std::move(frame));
        }
        queue_cond.notify_one();

        // Sleep to maintain 60 FPS
        next_frame_time += std::chrono::milliseconds(target_frame_duration);
        std::this_thread::sleep_until(next_frame_time);
    }
}

void ScreenRecorder::EncodeLoop()
{
    while (is_capturing || !frame_queue.empty())
    {
        std::vector<uint8_t> frame;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cond.wait(lock, [this]
                            { return !frame_queue.empty() || !is_capturing; });

            if (!frame_queue.empty())
            {
                frame = std::move(frame_queue.front());
                frame_queue.pop();
            }
        }

        if (!frame.empty())
        {
            AVFrame *av_frame = av_frame_alloc();
            av_frame->format = AV_PIX_FMT_YUV420P;
            av_frame->width = frame_width;
            av_frame->height = frame_height;
            av_frame_get_buffer(av_frame, 0);

            uint8_t *src_data[1] = {frame.data()};
            int src_linesize[1] = {frame_width * 4};
            sws_scale(sws_ctx, src_data, src_linesize, 0, frame_height, av_frame->data, av_frame->linesize);

            // Calculate PTS based on real elapsed time
            auto now = std::chrono::steady_clock::now();
            double elapsed_sec = std::chrono::duration<double>(now - start_time).count();
            av_frame->pts = static_cast<int64_t>(elapsed_sec / av_q2d(codec_ctx->time_base));

            AVPacket *pkt = av_packet_alloc();
            if (avcodec_send_frame(codec_ctx, av_frame) >= 0)
            {
                while (avcodec_receive_packet(codec_ctx, pkt) >= 0)
                {
                    av_packet_rescale_ts(pkt, codec_ctx->time_base, video_stream->time_base);
                    av_interleaved_write_frame(fmt_ctx, pkt);
                    av_packet_unref(pkt);
                }
            }

            av_packet_free(&pkt);
            av_frame_free(&av_frame);
        }
    }
}

void ScreenRecorder::Stop()
{
    is_capturing = false;
    if (capture_thread.joinable())
        capture_thread.join();
    if (encode_thread.joinable())
        encode_thread.join();

    if (fmt_ctx)
    {
        av_write_trailer(fmt_ctx);
        avio_closep(&fmt_ctx->pb);
        avformat_free_context(fmt_ctx);
    }

    if (codec_ctx)
        avcodec_free_context(&codec_ctx);
    if (sws_ctx)
        sws_freeContext(sws_ctx);
    if (surface)
        surface->Release();
    if (device)
        device->Release();
    if (d3d)
        d3d->Release();
}