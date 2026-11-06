#include "pi_tracker/camera_capture.h"
#include "pi_tracker/hailo_detector.h"
#include "pi_tracker/thread_safe_queue.h"
#include "pi_tracker/yuv420.h"

#include <opencv2/imgproc.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <optional>
#include <pthread.h>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace
{

    struct CameraFrame
    {
        std::int64_t timestampNs{};
        std::vector<std::uint8_t> i420; // повний кадр: YOLO потрібен колір
    };

    struct Result
    {
        double prepareMs{}; // I420 → RGB + зменшення до 640×640 на CPU
        double npuMs{};     // лише детектор на Hailo
        std::int64_t latencyNs{};
        std::vector<pi_tracker::Detection> detections;
    };

    double msSince(Clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    std::int64_t nowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
    }

    static_assert(std::atomic<bool>::is_always_lock_free);
    std::atomic<bool> g_stopRequested{false};

    void onSignal(int)
    {
        g_stopRequested = true;
    }

    void detectLoop(std::stop_token stop, pi_tracker::HailoDetector &detector,
                    pi_tracker::ThreadSafeQueue<CameraFrame> &frames, pi_tracker::ThreadSafeQueue<Result> &results,
                    int width, int height)
    {
        pthread_setname_np(pthread_self(), "d20-npu");
        cv::Mat rgb;
        cv::Mat input;
        while (!stop.stop_requested())
        {
            std::optional<CameraFrame> frame = frames.popFor(100ms);
            if (!frame)
            {
                continue;
            }
            Result result;
            const Clock::time_point start = Clock::now();
            // I420 у пам'яті — це «сіре» зображення висотою height·3/2: Y, під ним U і V
            const cv::Mat yuv{height * 3 / 2, width, CV_8UC1, frame->i420.data()};
            cv::cvtColor(yuv, rgb, cv::COLOR_YUV2RGB_I420);
            // Просто розтягуємо 16:9 у квадрат. Правильніше — letterbox (смуги згори і знизу), але для старту досить
            cv::resize(rgb, input, cv::Size{detector.inputWidth(), detector.inputHeight()});
            result.prepareMs = msSince(start);

            const Clock::time_point npuStart = Clock::now();
            result.detections = detector.detect({input.data, input.total() * input.elemSize()});
            result.npuMs = msSince(npuStart);
            result.latencyNs = nowNs() - frame->timestampNs;
            results.push(std::move(result));
        }
    }

    std::string describe(const std::vector<pi_tracker::Detection> &detections)
    {
        if (detections.empty())
        {
            return "nothing";
        }
        std::string text;
        for (const pi_tracker::Detection &d : detections)
        {
            text += std::format("{}{} {:.2f} at ({:.2f}, {:.2f})", text.empty() ? "" : "; ", pi_tracker::cocoName(d.classId),
                                d.score, (d.xMin + d.xMax) / 2.0f, (d.yMin + d.yMax) / 2.0f);
        }
        return text;
    }

} // namespace

int main(int argc, char *argv[])
{
    const std::string hef = argc > 1 ? argv[1] : "/usr/share/hailo-models/yolov8s_h8l.hef";
    const int seconds = argc > 2 ? std::stoi(argv[2]) : 0; // 0 — до Ctrl+C

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        pi_tracker::CaptureConfig config;
        const int width = static_cast<int>(config.width);
        const int height = static_cast<int>(config.height);

        // Порядок знищення знизу вгору: камера → воркер (jthread join) → черги → детектор
        pi_tracker::HailoDetector detector{hef};
        std::cerr << std::format("[npu] {} loaded, input {}x{}\n", hef, detector.inputWidth(), detector.inputHeight());
        pi_tracker::ThreadSafeQueue<CameraFrame> frames{1};
        pi_tracker::ThreadSafeQueue<Result> results{64};
        std::jthread worker{detectLoop, std::ref(detector), std::ref(frames), std::ref(results), width, height};
        pi_tracker::CameraCapture camera{config};
        if (camera.width() != config.width || camera.height() != config.height)
        {
            throw std::runtime_error(std::format("camera gives {}x{}, expected {}x{}", camera.width(), camera.height(),
                                                 config.width, config.height));
        }
        std::cerr << "[camera] " << camera.description() << '\n';

        const std::size_t frameBytes = pi_tracker::Yuv420View::frameSize(config.width, config.height);
        camera.start(
            [&frames, frameBytes](const pi_tracker::Frame &frame)
            {
                const auto bytes = frame.data.first(frameBytes);
                frames.push(CameraFrame{frame.timestampNs, {bytes.begin(), bytes.end()}});
            });

        const Clock::time_point deadline = Clock::now() + std::chrono::seconds{seconds};
        Clock::time_point nextReport = Clock::now() + 1s;
        std::size_t count = 0;
        double prepareSum = 0.0;
        double npuSum = 0.0;
        std::int64_t latencySum = 0;
        std::vector<pi_tracker::Detection> last;
        while (!g_stopRequested && (seconds == 0 || Clock::now() < deadline))
        {
            if (std::optional<Result> result = results.popFor(100ms))
            {
                ++count;
                prepareSum += result->prepareMs;
                npuSum += result->npuMs;
                latencySum += result->latencyNs;
                last = std::move(result->detections);
            }
            if (Clock::now() >= nextReport && count > 0)
            {
                const double n = static_cast<double>(count);
                std::cout << std::format("{:2} fps | prepare {:4.1f} ms | npu {:4.1f} ms | sensor->result {:5.1f} ms | {}",
                                         count, prepareSum / n, npuSum / n, static_cast<double>(latencySum) / n / 1e6,
                                         describe(last))
                          << std::endl;
                count = 0;
                prepareSum = npuSum = 0.0;
                latencySum = 0;
                nextReport += 1s;
            }
        }

        camera.stop();
        std::cerr << std::format("[camera] stopped, frames dropped before the NPU: {}\n", frames.dropped());
    }
    catch (const std::exception &e)
    {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
