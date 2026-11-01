#include "pi_tracker/camera_capture.h"
#include "pi_tracker/thread_safe_queue.h"
#include "pi_tracker/yuv420.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace
{

    // Те, що потік libcamera передає головному потоку: лише числа, без пікселів
    struct FrameInfo
    {
        std::uint32_t sequence{};
        std::int64_t timestampNs{}; // SensorTimestamp
        std::int64_t latencyNs{};   // від SensorTimestamp до початку нашого колбека
        std::int32_t exposureUs{};
        float analogueGain{};
        std::int64_t frameDurationUs{};
        double meanY{};
        std::int64_t processedLatencyNs{};
    };

    struct CopiedFrame
    {
        FrameInfo info;
        std::vector<std::uint8_t> luma;
    };

    // Статистика за одне вікно (≈1 с). Інтервал — різниця SensorTimestamp сусідніх кадрів
    class WindowStats
    {
    public:
        void add(const FrameInfo &info)
        {
            if (m_previous)
            {
                double interval = static_cast<double>(info.timestampNs - m_previous->timestampNs) / 1e6;
                m_intervalSumMs += interval;
                m_intervalSqSumMs += (interval * interval);

                if (interval < m_intervalMinMs)
                {
                    m_intervalMinMs = interval;
                }

                if (interval > m_intervalMaxMs)
                {
                    m_intervalMaxMs = interval;
                }

                std::uint32_t gap = info.sequence - m_previous->sequence;
                m_sequenceGaps += gap - 1;

                const long periods = std::lround(interval * 1000.0 / static_cast<double>(info.frameDurationUs));
                m_lost += static_cast<std::uint32_t>(std::max(0L, periods - 1));

                ++m_intervals;
            }

            m_latencySumNs += info.latencyNs;
            m_processedLatencySumNs += info.processedLatencyNs;
            ++m_frames;
            m_previous = info;
        }

        std::string summary() const
        {
            if (m_intervals == 0 || !m_previous)
            {
                return "no frames";
            }
            const double n = static_cast<double>(m_intervals);
            const double meanMs = m_intervalSumMs / n;
            const double jitterMs = std::sqrt(std::max(0.0, m_intervalSqSumMs / n - meanMs * meanMs));
            const double latencyMs = static_cast<double>(m_latencySumNs) / static_cast<double>(m_frames) / 1e6;
            const double processedMs = static_cast<double>(m_processedLatencySumNs) / static_cast<double>(m_frames) / 1e6;
            return std::format("{:5.1f} fps | interval {:.2f} ms (min {:.2f}, max {:.2f}, jitter {:.3f}) | lost {} (seq gaps {}) | "
                   "latency {:.1f} ms -> processed {:.1f} ms | exp {} us, gain {:.1f} | mean_y {:.1f}",
                   1000.0 / meanMs, meanMs, m_intervalMinMs, m_intervalMaxMs, jitterMs, m_lost, m_sequenceGaps,
                   latencyMs, processedMs, m_previous->exposureUs, m_previous->analogueGain, m_previous->meanY);
        }

        // Нове вікно. Попередній кадр лишаємо, щоб порахувати інтервал на межі вікон
        void reset()
        {
            const std::optional<FrameInfo> previous = m_previous;
            *this = WindowStats{};
            m_previous = previous;
        }

    private:
        std::optional<FrameInfo> m_previous;
        std::size_t m_frames{};
        std::size_t m_intervals{};
        std::uint32_t m_lost{};
        std::uint32_t m_sequenceGaps{};
        double m_intervalSumMs{};
        double m_intervalSqSumMs{};
        double m_intervalMinMs{std::numeric_limits<double>::max()};
        double m_intervalMaxMs{};
        std::int64_t m_latencySumNs{};
        std::int64_t m_processedLatencySumNs{};
    };

    double mean(std::span<const std::uint8_t> plane)
    {
        const std::uint64_t sum = std::accumulate(plane.begin(), plane.end(), std::uint64_t{0});
        return static_cast<double>(sum) / static_cast<double>(plane.size());
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

} // namespace

void workerLoop(std::stop_token stop, pi_tracker::ThreadSafeQueue<CopiedFrame> &frames,
                pi_tracker::ThreadSafeQueue<FrameInfo> &results, std::chrono::milliseconds workTime)
{
    pthread_setname_np(pthread_self(), "d16-worker");
    while (!stop.stop_requested())
    {
        auto frame = frames.popFor(100ms);
        if (!frame)
        {
            continue; // за 100 мс кадру не було: перевіряємо stop і чекаємо далі
        }
        frame->info.meanY = mean(frame->luma);
        std::this_thread::sleep_for(workTime); // імітація важкої обробки
        frame->info.processedLatencyNs = nowNs() - frame->info.timestampNs;
        results.push(frame->info);
    }
}

int main(int argc, char *argv[])
{
    const int seconds = argc > 1 ? std::stoi(argv[1]) : 10;
    pi_tracker::CaptureConfig config;
    config.fps = argc > 2 ? std::stod(argv[2]) : 30.0;
    config.bufferCount = argc > 3 ? static_cast<unsigned>(std::stoul(argv[3])) : 4;
    const std::chrono::milliseconds workTime{argc > 4 ? std::stoi(argv[4]) : 0}; // імітація важкої обробки в колбеку

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        pi_tracker::ThreadSafeQueue<FrameInfo> results{64}; // воркер → main: лише числа
        pi_tracker::ThreadSafeQueue<CopiedFrame> frames{1}; // камера → воркер: найсвіжіший кадр
        std::jthread worker{workerLoop, std::ref(frames), std::ref(results), workTime};
        pi_tracker::CameraCapture camera{config};

        std::cerr << "[camera] " << camera.description() << '\n';

        const unsigned width = camera.width();
        const unsigned height = camera.height();
        bool frameSaved = false; // читається й пишеться лише в потоці libcamera

        camera.start(
            [&frames, &frameSaved, width, height, workTime](const pi_tracker::Frame &frame)
            {
                const pi_tracker::Yuv420View view{frame.data, width, height};

                FrameInfo info;
                info.sequence = frame.sequence;
                info.timestampNs = frame.timestampNs;
                info.latencyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count() -
                                 frame.timestampNs;
                info.exposureUs = frame.exposureUs;
                info.analogueGain = frame.analogueGain;
                info.frameDurationUs = frame.frameDurationUs;

                // Через 2 с, коли AE зійшлася, зберігаємо один кадр: його можна відкрити інструментами Дня 15
                if (!frameSaved && frame.sequence >= 60)
                {
                    const auto bytes = frame.data.first(pi_tracker::Yuv420View::frameSize(width, height));
                    std::ofstream out{"data/day16_frame.yuv", std::ios::binary};
                    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                    frameSaved = true;
                }

                CopiedFrame copy = CopiedFrame();
                copy.info = info;
                copy.luma.assign(view.y().begin(), view.y().end());

                frames.push(std::move(copy));
            });

        WindowStats stats;
        const Clock::time_point deadline = Clock::now() + std::chrono::seconds{seconds};
        Clock::time_point nextReport = Clock::now() + 1s;
        while (!g_stopRequested && Clock::now() < deadline)
        {
            if (const auto info = results.popFor(100ms))
            {
                stats.add(*info);
            }
            if (Clock::now() >= nextReport)
            {
                std::cout << stats.summary() << std::endl;
                stats.reset();
                nextReport += 1s;
            }
        }

        camera.stop();
        std::cerr << std::format("[camera] stopped, queue dropped {}\n", frames.dropped());
    }
    catch (const std::exception &e)
    {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}