#include "pi_tracker/camera_capture.h"

#include <cmath>
#include <format>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace pi_tracker
{

    namespace
    {
        // libcamera повертає int: 0 — успіх, від'ємне — -errno. Винятків вона не кидає
        void check(int result, const char *what)
        {
            if (result < 0)
            {
                throw std::runtime_error(std::format("{}: error {}", what, result));
            }
        }
    } // namespace

    CameraCapture::CameraCapture(const CaptureConfig &config)
        : m_fps{config.fps}, m_width{config.width}, m_height{config.height}
    {
        // 1. Менеджер знаходить камери (як CameraManager в Android)
        m_manager = std::make_unique<libcamera::CameraManager>();
        check(m_manager->start(), "CameraManager::start");
        if (m_manager->cameras().empty())
        {
            throw std::runtime_error("no cameras found");
        }

        // 2. Захоплюємо камеру ексклюзивно: другий процес отримає -EBUSY
        m_camera = m_manager->cameras().front();
        check(m_camera->acquire(), "Camera::acquire (camera busy?)");

        // 3. Конфігурація: одна роль = один потік. VideoRecording — безперервне відео
        m_config = m_camera->generateConfiguration({libcamera::StreamRole::VideoRecording});
        m_config->orientation = libcamera::Orientation::Rotate180;
        libcamera::StreamConfiguration &streamConfig = m_config->at(0);
        streamConfig.size = {config.width, config.height};
        streamConfig.pixelFormat = libcamera::formats::YUV420;
        streamConfig.colorSpace = libcamera::ColorSpace::Sycc; // повний діапазон 0…255
        streamConfig.bufferCount = config.bufferCount;

        // validate() може підправити запит під можливості заліза: Valid / Adjusted / Invalid
        const libcamera::CameraConfiguration::Status status = m_config->validate();
        if (status == libcamera::CameraConfiguration::Invalid)
        {
            throw std::runtime_error("camera configuration invalid");
        }
        if (status == libcamera::CameraConfiguration::Adjusted)
        {
            std::cerr << std::format("[camera] configuration adjusted to {}\n", streamConfig.toString());
        }
        check(m_camera->configure(m_config.get()), "Camera::configure");

        m_stream = streamConfig.stream();
        m_width = streamConfig.size.width;
        m_height = streamConfig.size.height;
        if (streamConfig.stride != m_width)
        {
            throw std::runtime_error(std::format("stride {} != width {}: rows are padded, Yuv420View can't read this frame",
                                                 streamConfig.stride, m_width));
        }

        // 4. Буфери: драйвер виділяє dmabuf, ми відображаємо кожен у свою пам'ять
        m_allocator = std::make_unique<libcamera::FrameBufferAllocator>(m_camera);
        check(m_allocator->allocate(m_stream), "FrameBufferAllocator::allocate");

        const auto &buffers = m_allocator->buffers(m_stream);
        for (std::size_t i = 0; i < buffers.size(); ++i)
        {
            // Площини Y, U, V лежать в одному dmabuf з різними зсувами (offset), тому відображаємо його один раз
            const auto planes = buffers[i]->planes();
            const libcamera::FrameBuffer::Plane &last = planes.back();
            m_mappings.emplace_back(planes.front().fd.get(), std::size_t{last.offset} + last.length);

            // 5. Запит = «заповни цей буфер». cookie = індекс буфера, щоб у колбеку знайти його відображення
            std::unique_ptr<libcamera::Request> request = m_camera->createRequest(i);
            if (!request)
            {
                throw std::runtime_error("Camera::createRequest failed");
            }
            check(request->addBuffer(m_stream, buffers[i].get()), "Request::addBuffer");
            m_requests.push_back(std::move(request));
        }
    }

    CameraCapture::~CameraCapture()
    {
        stop();
        // Явний порядок: запити й буфери звільняємо до release(), менеджер зупиняємо останнім
        m_requests.clear();
        m_mappings.clear();
        m_allocator.reset();
        m_camera->release();
        m_camera.reset();
        m_manager->stop();
    }

    void CameraCapture::start(FrameCallback callback)
    {
        m_callback = std::move(callback);
        m_camera->requestCompleted.connect(this, &CameraCapture::onRequestCompleted);

        // Керування на старті: фіксуємо тривалість кадру = 1/fps (мін і макс однакові)
        const auto frameUs = static_cast<std::int64_t>(std::lround(1'000'000.0 / m_fps));
        libcamera::ControlList controls;
        controls.set(libcamera::controls::FrameDurationLimits, {frameUs, frameUs});
        check(m_camera->start(&controls), "Camera::start");
        m_running = true;

        // Віддаємо камері всі запити одразу: вона заповнюватиме їх по черзі
        for (const auto &request : m_requests)
        {
            check(m_camera->queueRequest(request.get()), "Camera::queueRequest");
        }
    }

    void CameraCapture::stop()
    {
        if (!m_running)
        {
            return;
        }
        // Спершу прапорець, щоб колбек перестав повертати запити камері.
        // stop() синхронний: запити, що ще в черзі, повертаються з RequestCancelled
        m_running = false;
        m_camera->stop();
        m_camera->requestCompleted.disconnect(this);
    }

    std::string CameraCapture::description() const
    {
        const libcamera::StreamConfiguration &streamConfig = m_config->at(0);
        const std::string model{m_camera->properties().get(libcamera::properties::Model).value_or("unknown")};
        return std::format("{} {} stride {}, {} buffers", model, streamConfig.toString(), streamConfig.stride,
                           m_requests.size());
    }

    // Виконується в потоці libcamera. Тут не можна довго блокуватися: поки ми тут, буфер не в камері
    void CameraCapture::onRequestCompleted(libcamera::Request *request)
    {
        if (request->status() == libcamera::Request::RequestCancelled)
        {
            return; // так завершуються запити під час stop(); буфер порожній
        }

        const libcamera::FrameBuffer *buffer = request->buffers().at(m_stream);
        const libcamera::ControlList &metadata = request->metadata();

        Frame frame;
        frame.sequence = buffer->metadata().sequence;
        frame.timestampNs = metadata.get(libcamera::controls::SensorTimestamp).value_or(0);
        frame.exposureUs = metadata.get(libcamera::controls::ExposureTime).value_or(0);
        frame.analogueGain = metadata.get(libcamera::controls::AnalogueGain).value_or(0.0f);
        frame.frameDurationUs = metadata.get(libcamera::controls::FrameDuration).value_or(0);
        frame.data = m_mappings.at(request->cookie()).data();

        m_callback(frame);

        // Повертаємо той самий буфер камері: reuse скидає статус, але лишає буфер прив'язаним
        if (m_running)
        {
            request->reuse(libcamera::Request::ReuseBuffers);
            m_camera->queueRequest(request);
        }
    }

} // namespace pi_tracker