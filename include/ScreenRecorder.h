#ifndef SCREENRECORDER_H
#define SCREENRECORDER_H

#include <windows.h>
#include <d3d9.h>
#include <vector>
#include <string>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

class ScreenRecorder {
public:
    enum Resolution {
        UHD_4K, // 3840x2160
        FHD,    // 1920x1080
        HD,     // 1280x720
        SD      // 854x480
    };

    ScreenRecorder();
    ~ScreenRecorder();

    bool Initialize(const std::string& outputFile, Resolution resolution);
    void Start();
    void Stop();

private:
    IDirect3D9* d3d;
    IDirect3DDevice9* device;
    IDirect3DSurface9* surface;

    AVFormatContext* fmt_ctx;
    AVStream* video_stream;
    AVCodecContext* codec_ctx;
    SwsContext* sws_ctx;

    int frame_width;
    int frame_height;
    std::chrono::steady_clock::time_point start_time;

    // Threading and queue
    std::queue<std::vector<uint8_t>> frame_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cond;
    std::atomic<bool> is_capturing;
    std::thread capture_thread;
    std::thread encode_thread;

    void CaptureLoop();
    void EncodeLoop();
    bool InitDirect3D();
    bool InitFFmpeg(const std::string& outputFile);
    std::pair<int, int> GetResolution(Resolution resolution);
};

#endif // SCREENRECORDER_H