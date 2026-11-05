#include "pi_tracker/camera_capture.h"
#include "pi_tracker/marker_detector.h"
#include "pi_tracker/thread_safe_queue.h"
#include "pi_tracker/yuv420.h"

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

    // Камера → воркер: копія площини Y. Буфер камери після колбека вже не наш
    struct LumaFrame
    {
        std::int64_t timestampNs{}; // SensorTimestamp
        std::vector<std::uint8_t> luma;
    };

    // Воркер → main: що знайшли і скільки це коштувало
    struct Detection
    {
        double detectMs{};          // лише detectMarkers
        std::int64_t latencyNs{};   // від сенсора до готового результату
        std::vector<pi_tracker::Marker> markers;
    };

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

    // Окремий потік: детектор повільніший за колбек, а колбек камери блокувати не можна (День 16)
    void detectLoop(std::stop_token stop, pi_tracker::ThreadSafeQueue<LumaFrame> &frames,
                    pi_tracker::ThreadSafeQueue<Detection> &results, int width, int height)
    {
        pthread_setname_np(pthread_self(), "d19-detect");
        const pi_tracker::MarkerDetector detector;
        while (!stop.stop_requested())
        {
            std::optional<LumaFrame> frame = frames.popFor(100ms);
            if (!frame)
            {
                continue;
            }
            const Clock::time_point start = Clock::now();
            const cv::Mat gray = pi_tracker::wrapGray(frame->luma, width, height); // без копії: вікно у frame->luma
            Detection detection;
            detection.markers = detector.detect(gray);
            detection.detectMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            detection.latencyNs = nowNs() - frame->timestampNs;
            results.push(std::move(detection));
        }
    }

    std::string describe(const pi_tracker::Marker &m)
    {
        return std::format("id {}: offset x {:+.2f} y {:+.2f}, side {:.0f} px", m.id, m.offset.x, m.offset.y, m.sidePx);
    }

} // namespace

int main(int argc, char *argv[])
{
    const int seconds = argc > 1 ? std::stoi(argv[1]) : 0; // 0 — до Ctrl+C

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        pi_tracker::CaptureConfig config; // 1280×720, 30 fps
        const int width = static_cast<int>(config.width);
        const int height = static_cast<int>(config.height);

        // Порядок важливий (знищення знизу вгору): спершу камера перестане класти кадри,
        // потім jthread зупинить і дочекається воркера, і лише тоді зникнуть черги
        pi_tracker::ThreadSafeQueue<LumaFrame> frames{1};      // лише найсвіжіший кадр
        pi_tracker::ThreadSafeQueue<Detection> results{64};
        std::jthread worker{detectLoop, std::ref(frames), std::ref(results), width, height};
        pi_tracker::CameraCapture camera{config};
        if (camera.width() != config.width || camera.height() != config.height)
        {
            throw std::runtime_error(std::format("camera gives {}x{}, detector expects {}x{}", camera.width(),
                                                 camera.height(), config.width, config.height));
        }
        std::cerr << "[camera] " << camera.description() << '\n';

        camera.start(
            [&frames, &config](const pi_tracker::Frame &frame)
            {
                const pi_tracker::Yuv420View view{frame.data, config.width, config.height};
                frames.push(LumaFrame{frame.timestampNs, {view.y().begin(), view.y().end()}});
            });

        const Clock::time_point deadline = Clock::now() + std::chrono::seconds{seconds};
        Clock::time_point nextReport = Clock::now() + 1s;
        std::size_t count = 0;
        double detectSumMs = 0.0;
        std::int64_t latencySumNs = 0;
        std::optional<Detection> last;
        while (!g_stopRequested && (seconds == 0 || Clock::now() < deadline))
        {
            if (std::optional<Detection> detection = results.popFor(100ms))
            {
                ++count;
                detectSumMs += detection->detectMs;
                latencySumNs += detection->latencyNs;
                last = std::move(detection);
            }
            if (Clock::now() >= nextReport && count > 0)
            {
                const double n = static_cast<double>(count);
                std::string seen = "no marker";
                if (!last->markers.empty())
                {
                    seen = describe(last->markers.front());
                    if (last->markers.size() > 1)
                    {
                        seen += std::format(" (+{} more)", last->markers.size() - 1);
                    }
                }
                std::cout << std::format("{:2} frames | detect {:4.1f} ms | sensor->result {:4.1f} ms | {}", count,
                                         detectSumMs / n, static_cast<double>(latencySumNs) / n / 1e6, seen)
                          << std::endl;
                count = 0;
                detectSumMs = 0.0;
                latencySumNs = 0;
                nextReport += 1s;
            }
        }

        camera.stop();
        std::cerr << std::format("[camera] stopped, frames dropped before detection: {}\n", frames.dropped());
    }
    catch (const std::exception &e)
    {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
