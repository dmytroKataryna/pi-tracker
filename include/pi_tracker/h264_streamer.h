#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>

#include <gst/gst.h>

namespace pi_tracker
{

    // RAII для об'єктів GStreamer: unique_ptr сам викличе gst_object_unref, коли вийде з області видимості.
    // Те саме, що Kotlin `use { }` чи Java try-with-resources, але без жодного блоку: працює деструктор
    struct GstObjectUnref
    {
        void operator()(gpointer object) const
        {
            gst_object_unref(object);
        }
    };

    struct GstBufferUnref
    {
        void operator()(GstBuffer *buff) const
        {
            gst_buffer_unref(buff);
        }
    };

    // Для елементів, шини (bus), pad-ів — усього, що є GstObject
    template <typename T>
    using GstPtr = std::unique_ptr<T, GstObjectUnref>;

    using GstBufferPtr = std::unique_ptr<GstBuffer, GstBufferUnref>;

    struct StreamConfig
    {
        std::string host; // IP приймача (Mac)
        std::uint16_t port{5600};
        unsigned width{1280};
        unsigned height{720};
        unsigned fps{30};
        unsigned bitrateKbps{2000};
        unsigned keyframeInterval{30}; // кадрів між ключовими кадрами (або період intra-refresh)
        bool intraRefresh{false};
    };

    // Статистика від попереднього виклику takeStats()
    struct StreamStats
    {
        std::size_t pushed{};  // кадрів віддали в appsrc
        std::size_t encoded{}; // кадрів вийшло з кодера
        std::size_t bytes{};   // сума розмірів закодованих кадрів
        std::size_t maxFrameBytes{};
        std::int64_t latencySumNs{}; // від SensorTimestamp до виходу з кодера
        std::int64_t latencyMaxNs{};
    };

    // Наш кадр I420 → appsrc → x264enc → rtph264pay → udpsink.
    // Той самий конвеєр, що в gst-launch на Дні 17, тільки замість libcamerasrc — наш CameraCapture
    class H264Streamer
    {
    public:
        explicit H264Streamer(const StreamConfig &config);
        ~H264Streamer();

        H264Streamer(const H264Streamer &) = delete;
        H264Streamer &operator=(const H264Streamer &) = delete;

        // Викликається з потоку камери. Копіює кадр у буфер GStreamer і кладе в чергу appsrc, не чекаючи кодера
        bool push(std::span<const std::uint8_t> i420, std::int64_t timestampNs);

        // Головний потік: забирає статистику і починає нову
        StreamStats takeStats();

        // Головний потік: текст помилки з шини (bus), якщо конвеєр зламався
        std::optional<std::string> pollError();

    private:
        // C-колбек: GStreamer не знає про класи, тому `this` приходить як void* (gpointer)
        static GstPadProbeReturn onEncoded(GstPad *pad, GstPadProbeInfo *info, gpointer self);

        GstPtr<GstElement> m_pipeline;
        GstPtr<GstElement> m_appsrc;
        GstPtr<GstBus> m_bus;
        std::size_t m_frameBytes{};
        std::int64_t m_frameDurationNs{};
        // Пише потік камери (перший кадр), читає потік кодера → atomic
        std::atomic<std::int64_t> m_firstTimestampNs{-1};

        // Статистику чіпають три потоки: камера (pushed), кодер (решта), main (takeStats)
        std::mutex m_statsMutex;
        StreamStats m_stats;
    };

} // namespace pi_tracker
