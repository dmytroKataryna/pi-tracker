#include "pi_tracker/h264_streamer.h"

#include <gst/app/gstappsrc.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>
#include <utility>

namespace pi_tracker
{

    namespace
    {
        std::int64_t steadyNowNs()
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }

        // Рядок конвеєра — той самий синтаксис, що в gst-launch-1.0
        std::string pipelineDescription(const StreamConfig &config)
        {
            // appsrc: наша «точка завантаження». max-buffers=2 + leaky-type=downstream:
            // якщо кодер не встигає, викидаємо найстаріший кадр (як ваша ThreadSafeQueue{2} на Дні 16).
            // colorimetry=1:4:0:0 = повний діапазон 0…255, BT.601 — саме такий YUV дає камера (sYCC)
            return std::format(
                "appsrc name=src is-live=true format=time max-buffers=2 leaky-type=downstream "
                "caps=video/x-raw,format=I420,width={},height={},framerate={}/1,colorimetry=1:4:0:0 "
                "! x264enc name=enc tune=zerolatency speed-preset=ultrafast bitrate={} key-int-max={} intra-refresh={} "
                "! video/x-h264,profile=baseline "
                "! rtph264pay config-interval=1 pt=96 "
                "! udpsink host={} port={} sync=false",
                config.width, config.height, config.fps, config.bitrateKbps, config.keyframeInterval,
                config.intraRefresh ? "true" : "false", config.host, config.port);
        }
    } // namespace

    H264Streamer::H264Streamer(const StreamConfig &config)
        : m_frameBytes{std::size_t{config.width} * config.height * 3 / 2},
          m_frameDurationNs{1'000'000'000 / config.fps}
    {
        gst_init(nullptr, nullptr);

        // C API не кидає винятків: помилка приходить через GError**
        GError *error = nullptr;
        m_pipeline.reset(gst_parse_launch(pipelineDescription(config).c_str(), &error));
        if (error != nullptr)
        {
            const std::string message = error->message;
            g_error_free(error);
            throw std::runtime_error("gst_parse_launch: " + message);
        }

        // get_by_name повертає нове посилання (transfer full) → віддаємо його unique_ptr
        m_appsrc.reset(gst_bin_get_by_name(GST_BIN(m_pipeline.get()), "src"));
        GstPtr<GstElement> encoder{gst_bin_get_by_name(GST_BIN(m_pipeline.get()), "enc")};
        if (!m_appsrc || !encoder)
        {
            throw std::runtime_error("pipeline has no src/enc element");
        }

        // Зонд (probe) на виході кодера: GStreamer викличе onEncoded для кожного закодованого кадру
        GstPtr<GstPad> encoded{gst_element_get_static_pad(encoder.get(), "src")};
        gst_pad_add_probe(encoded.get(), GST_PAD_PROBE_TYPE_BUFFER, &H264Streamer::onEncoded, this, nullptr);

        m_bus.reset(gst_element_get_bus(m_pipeline.get()));

        if (gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        {
            throw std::runtime_error("pipeline failed to start");
        }
    }

    H264Streamer::~H264Streamer()
    {
        // Зупиняємо потоки GStreamer до того, як unique_ptr звільнять елементи
        gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);
    }

    bool H264Streamer::push(std::span<const std::uint8_t> i420, std::int64_t timestampNs)
    {
        if (i420.size() < m_frameBytes)
        {
            return false;
        }

        if (m_firstTimestampNs.load() < 0)
        {
            m_firstTimestampNs.store(timestampNs); // перший кадр = нуль часу стріму
        }
        const std::int64_t first = m_firstTimestampNs.load();

        auto buffer = GstBufferPtr(gst_buffer_new_allocate(nullptr, m_frameBytes, nullptr));
        auto bufferCopySize = gst_buffer_fill(buffer.get(), 0, i420.data(), m_frameBytes);
        if (bufferCopySize != m_frameBytes)
        {
            return false;
        }

        GST_BUFFER_PTS(buffer.get()) = static_cast<GstClockTime>(timestampNs - first);
        GST_BUFFER_DURATION(buffer.get()) = static_cast<GstClockTime>(m_frameDurationNs);
        auto result = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc.get()), buffer.release());

        {
            std::lock_guard lock{m_statsMutex};
            ++m_stats.pushed;
        }

        if (result == GST_FLOW_OK)
        {
            return true;
        }
        return false;
    }

    GstPadProbeReturn H264Streamer::onEncoded(GstPad *pad, GstPadProbeInfo *info, gpointer self)
    {
        auto *streamer = static_cast<H264Streamer *>(self);
        GstBuffer *buffer = gst_pad_probe_info_get_buffer(info);

        const std::size_t size = gst_buffer_get_size(buffer);

        // x264enc зсуває PTS на 1000 годин (щоб DTS не був від'ємним), а зсув записує в сегмент.
        // Сегмент переводить PTS назад у «час від старту» — той самий, що ми поставили в push()
        GstEvent *segmentEvent = gst_pad_get_sticky_event(pad, GST_EVENT_SEGMENT, 0);
        if (segmentEvent == nullptr)
        {
            return GST_PAD_PROBE_OK;
        }
        const GstSegment *segment = nullptr;
        gst_event_parse_segment(segmentEvent, &segment);
        const GstClockTime streamTimeNs = gst_segment_to_running_time(segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
        gst_event_unref(segmentEvent);

        // перший SensorTimestamp + час від старту = коли сенсор зняв цей кадр
        const std::int64_t captureNs = streamer->m_firstTimestampNs.load() + static_cast<std::int64_t>(streamTimeNs);
        const std::int64_t latencyNs = steadyNowNs() - captureNs;

        std::lock_guard lock{streamer->m_statsMutex};
        StreamStats &stats = streamer->m_stats;
        ++stats.encoded;
        stats.bytes += size;
        stats.maxFrameBytes = std::max(stats.maxFrameBytes, size);
        stats.latencySumNs += latencyNs;
        stats.latencyMaxNs = std::max(stats.latencyMaxNs, latencyNs);
        return GST_PAD_PROBE_OK;
    }

    StreamStats H264Streamer::takeStats()
    {
        std::lock_guard lock{m_statsMutex};
        return std::exchange(m_stats, StreamStats{});
    }

    std::optional<std::string> H264Streamer::pollError()
    {
        GstMessage *message = gst_bus_pop_filtered(m_bus.get(), GST_MESSAGE_ERROR);
        if (message == nullptr)
        {
            return std::nullopt;
        }
        GError *error = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        std::string text = std::format("{}: {}", GST_OBJECT_NAME(GST_MESSAGE_SRC(message)), error->message);
        g_error_free(error);
        g_free(debug);
        gst_message_unref(message);
        return text;
    }

} // namespace pi_tracker
