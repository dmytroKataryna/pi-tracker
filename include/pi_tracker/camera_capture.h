#pragma once

#include "pi_tracker/mapped_buffer.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <libcamera/libcamera.h>

namespace pi_tracker
{

    struct CaptureConfig
    {
        unsigned width{1280};
        unsigned height{720};
        double fps{30.0};
        unsigned bufferCount{4};
        bool rotate180{true};
    };

    // Один кадр очима споживача. data — view у буфер камери:
    // дійсний ЛИШЕ всередині колбека, після нього буфер повертається камері й перезаписується
    struct Frame
    {
        std::uint32_t sequence{};           // номер кадру (чи видно в ньому втрачені кадри — перевірите в Частині 4)
        std::int64_t timestampNs{};         // SensorTimestamp, нс; на Pi той самий годинник, що std::chrono::steady_clock
        std::int32_t exposureUs{};          // фактична витримка цього кадру
        float analogueGain{};               // фактичне підсилення
        std::int64_t frameDurationUs{};     // тривалість кадру, яку сенсор фактично використав
        std::span<const std::uint8_t> data; // кадр I420: Y, потім U і V, без відступів (stride == width)
    };

    using FrameCallback = std::function<void(const Frame &)>;

    // Камера через libcamera: налаштування потоку YUV420, буфери, запити і колбек на кожен кадр
    class CameraCapture
    {
    public:
        explicit CameraCapture(const CaptureConfig &config);
        ~CameraCapture();

        CameraCapture(const CameraCapture &) = delete;
        CameraCapture &operator=(const CameraCapture &) = delete;

        // callback викликається в потоці libcamera, а не в тому, що викликав start()
        void start(FrameCallback callback);
        void stop();

        // Наприклад: "imx708 1280x720-YUV420/sYCC stride 1280, 4 buffers"
        std::string description() const;
        unsigned width() const { return m_width; }
        unsigned height() const { return m_height; }

    private:
        void onRequestCompleted(libcamera::Request *request);

        // Порядок важливий: члени знищуються знизу вгору
        std::unique_ptr<libcamera::CameraManager> m_manager;
        std::shared_ptr<libcamera::Camera> m_camera;
        std::unique_ptr<libcamera::CameraConfiguration> m_config;
        libcamera::Stream *m_stream{nullptr};
        std::unique_ptr<libcamera::FrameBufferAllocator> m_allocator;
        std::vector<MappedBuffer> m_mappings;
        std::vector<std::unique_ptr<libcamera::Request>> m_requests;
        FrameCallback m_callback;
        double m_fps{};
        unsigned m_width{};
        unsigned m_height{};
        std::atomic<bool> m_running{false}; // читається в потоці libcamera, пишеться в нашому
    };

} // namespace pi_tracker