#include "ScreenRecorder.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>

std::string GetTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::tm buf;
    localtime_s(&buf, &in_time_t);

    std::stringstream ss;
    ss << std::put_time(&buf, "%Y-%m-%d-%H-%M-%S");
    return ss.str();
}

int main()
{
    ScreenRecorder recorder;

    std::cout << "Select resolution:\n"
              << "1. 4K (3840x2160)\n"
              << "2. 1080p (1920x1080)\n"
              << "3. 720p (1280x720)\n"
              << "4. 480p (854x480)\n"
              << "Enter choice (1-4): ";

    int choice;
    std::cin >> choice;

    ScreenRecorder::Resolution resolution;
    switch (choice)
    {
    case 1:
        resolution = ScreenRecorder::UHD_4K;
        break;
    case 2:
        resolution = ScreenRecorder::FHD;
        break;
    case 3:
        resolution = ScreenRecorder::HD;
        break;
    case 4:
        resolution = ScreenRecorder::SD;
        break;
    default:
        resolution = ScreenRecorder::FHD;
    }

    std::string outputFile = "SR_" + GetTimestamp() + ".mp4";

    if (!recorder.Initialize(outputFile, resolution))
    {
        std::cerr << "Failed to initialize screen recorder!" << std::endl;
        return -1;
    }

    recorder.Start();
    std::cout << "Recording started. Press SPACE to stop..." << std::endl;

    while (!(GetAsyncKeyState(VK_SPACE) & 0x8000))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    recorder.Stop();
    std::cout << "Recording stopped. Video saved to " << outputFile << std::endl;
    return 0;
}