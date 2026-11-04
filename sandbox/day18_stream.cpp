#include "pi_tracker/camera_capture.h"
#include "pi_tracker/h264_streamer.h"
#include "pi_tracker/yuv420.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

namespace
{

    static_assert(std::atomic<bool>::is_always_lock_free);
    std::atomic<bool> g_stopRequested{false};

    void onSignal(int)
    {
        g_stopRequested = true;
    }

    std::string format(const pi_tracker::StreamStats &s)
    {
        if (s.encoded == 0)
        {
            return std::format("pushed {}, nothing encoded yet", s.pushed);
        }
        const double n = static_cast<double>(s.encoded);
        return std::format("{} in -> {} out | {:4.0f} kbit/s | frame avg {:5.1f} KB, max {:5.1f} KB | "
                           "sensor->encoded avg {:4.1f} ms, max {:4.1f} ms",
                           s.pushed, s.encoded, static_cast<double>(s.bytes) * 8.0 / 1000.0,
                           static_cast<double>(s.bytes) / n / 1024.0, static_cast<double>(s.maxFrameBytes) / 1024.0,
                           static_cast<double>(s.latencySumNs) / n / 1e6, static_cast<double>(s.latencyMaxNs) / 1e6);
    }

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: day18_stream <receiver-ip> [bitrate_kbps] [--intra-refresh]\n";
        return EXIT_FAILURE;
    }

    pi_tracker::StreamConfig streamConfig;
    streamConfig.host = argv[1];
    streamConfig.bitrateKbps = argc > 2 ? static_cast<unsigned>(std::stoul(argv[2])) : 2000;
    streamConfig.intraRefresh = argc > 3 && std::string_view{argv[3]} == "--intra-refresh";

    pi_tracker::CaptureConfig captureConfig;
    captureConfig.width = streamConfig.width;
    captureConfig.height = streamConfig.height;
    captureConfig.fps = streamConfig.fps;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        // Порядок важливий: члени й локальні змінні знищуються знизу вгору.
        // Стрімер оголошено ПЕРШИМ → камера знищиться раніше і вже не викличе push() у знищений стрімер
        pi_tracker::H264Streamer streamer{streamConfig};
        pi_tracker::CameraCapture camera{captureConfig};

        if (camera.width() != streamConfig.width || camera.height() != streamConfig.height)
        {
            throw std::runtime_error(std::format("camera gives {}x{}, stream expects {}x{}", camera.width(),
                                                 camera.height(), streamConfig.width, streamConfig.height));
        }
        std::cerr << "[camera] " << camera.description() << '\n';
        std::cerr << std::format("[stream] RTP H.264 -> {}:{}, {} kbit/s, {}\n", streamConfig.host, streamConfig.port,
                                 streamConfig.bitrateKbps,
                                 streamConfig.intraRefresh ? "intra-refresh" : "IDR every 30 frames");

        const std::size_t frameBytes = pi_tracker::Yuv420View::frameSize(camera.width(), camera.height());

        // Потік libcamera: лише копія в GStreamer (≈0.6 мс), кодує вже потік appsrc
        camera.start([&streamer, frameBytes](const pi_tracker::Frame &frame)
                     { streamer.push(frame.data.first(frameBytes), frame.timestampNs); });

        while (!g_stopRequested)
        {
            for (int i = 0; i < 10 && !g_stopRequested; ++i)
            {
                std::this_thread::sleep_for(100ms);
            }
            if (const auto error = streamer.pollError())
            {
                throw std::runtime_error(*error);
            }
            std::cout << format(streamer.takeStats()) << std::endl;
        }

        camera.stop();
        std::cerr << "[stream] stopped\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
