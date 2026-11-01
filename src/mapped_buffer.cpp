#include "pi_tracker/mapped_buffer.h"
#include <sys/mman.h>
#include <cerrno>
#include <system_error>
#include <utility>

namespace pi_tracker
{

    MappedBuffer::MappedBuffer(int fd, std::size_t length) : m_address{::mmap(nullptr, length, PROT_READ, MAP_SHARED, fd, 0)}, m_length{length}
    {
        if (m_address == MAP_FAILED)
        {
            throw std::system_error(errno, std::generic_category(), "mmap");
        }
    }

    MappedBuffer::~MappedBuffer()
    {
        if (m_address != nullptr)
        {
            ::munmap(m_address, m_length);
        }
    }

    MappedBuffer::MappedBuffer(MappedBuffer &&other) noexcept
    {
        m_address = other.m_address;
        m_length = other.m_length;

        other.m_address = nullptr;
        other.m_length = 0;
    }

    MappedBuffer &MappedBuffer::operator=(MappedBuffer &&other) noexcept
    {
        if (this != &other)
        {
            if (m_address != nullptr)
            {
                ::munmap(m_address, m_length);
            }
            m_address = std::exchange(other.m_address, nullptr);
            m_length = std::exchange(other.m_length, 0);
        }
        return *this;
    }

    std::span<const std::uint8_t> MappedBuffer::data() const
    {
        return {static_cast<const std::uint8_t *>(m_address), m_length};
    }

}