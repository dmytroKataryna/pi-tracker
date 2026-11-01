#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace pi_tracker
{

    // RAII-обгортка над mmap: відображає буфер камери (dmabuf) у нашу пам'ять лише для читання.
    class MappedBuffer
    {
    public:
        // Кидає std::system_error, якщо mmap не вдався
        MappedBuffer(int fd, std::size_t length);
        ~MappedBuffer();

        MappedBuffer(const MappedBuffer &) = delete;
        MappedBuffer &operator=(const MappedBuffer &) = delete;
        MappedBuffer(MappedBuffer &&other) noexcept;
        MappedBuffer &operator=(MappedBuffer &&other) noexcept;

        std::span<const std::uint8_t> data() const;

    private:
        void *m_address{nullptr};
        std::size_t m_length{0};
    };

} // namespace pi_tracker