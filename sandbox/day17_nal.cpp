#include "pi_tracker/h264_nal.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

    std::vector<std::uint8_t> readFile(const std::string &path)
    {
        std::ifstream in{path, std::ios::binary};
        if (!in)
        {
            throw std::runtime_error(std::format("cannot open {}", path));
        }
        return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    }

    struct TypeStats
    {
        std::size_t count{};
        std::size_t bytes{};
    };

    struct FrameStats
    {
        std::size_t bytes{}; // сума слайсів кадру, без SPS/PPS/SEI
        bool idr{};
    };

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: day17_nal file.h264 [fps]\n";
        return EXIT_FAILURE;
    }
    const double fps = argc > 2 ? std::stod(argv[2]) : 30.0;

    try
    {
        const std::vector<std::uint8_t> stream = readFile(argv[1]);
        const std::vector<pi_tracker::NalUnit> units = pi_tracker::splitAnnexB(stream);

        std::array<TypeStats, 32> byType{};
        std::vector<FrameStats> frames;
        for (const pi_tracker::NalUnit &unit : units)
        {
            const std::uint8_t type = unit.type();
            ++byType[type].count;
            byType[type].bytes += unit.bytes.size();

            if (unit.startsFrame())
            {
                frames.push_back(FrameStats{0, type == 5});
            }
            if ((type == 1 || type == 5) && !frames.empty())
            {
                frames.back().bytes += unit.bytes.size();
            }
        }

        std::cout << std::format("{}: {} bytes, {} NAL units, {} frames\n\n", argv[1], stream.size(), units.size(),
                                 frames.size());
        std::cout << "type  name    count      bytes    avg\n";
        for (std::size_t type = 0; type < byType.size(); ++type)
        {
            const TypeStats &s = byType[type];
            if (s.count > 0)
            {
                std::cout << std::format("{:4}  {:6} {:6} {:10} {:6}\n", type,
                                         pi_tracker::nalTypeName(static_cast<std::uint8_t>(type)), s.count, s.bytes,
                                         s.bytes / s.count);
            }
        }

        // Інтервал між ключовими кадрами і середній розмір ключового та звичайного кадру
        std::vector<std::size_t> idrIndices;
        std::size_t idrBytes = 0;
        std::size_t otherBytes = 0;
        for (std::size_t i = 0; i < frames.size(); ++i)
        {
            if (frames[i].idr)
            {
                idrIndices.push_back(i);
                idrBytes += frames[i].bytes;
            }
            else
            {
                otherBytes += frames[i].bytes;
            }
        }
        if (idrIndices.size() >= 2)
        {
            std::size_t minGap = frames.size();
            std::size_t maxGap = 0;
            for (std::size_t i = 1; i < idrIndices.size(); ++i)
            {
                minGap = std::min(minGap, idrIndices[i] - idrIndices[i - 1]);
                maxGap = std::max(maxGap, idrIndices[i] - idrIndices[i - 1]);
            }
            std::cout << std::format("\nkeyframe every {}…{} frames\n", minGap, maxGap);
        }
        const std::size_t otherCount = frames.size() - idrIndices.size();
        if (!idrIndices.empty() && otherCount > 0)
        {
            const double idrAvg = static_cast<double>(idrBytes) / static_cast<double>(idrIndices.size());
            const double otherAvg = static_cast<double>(otherBytes) / static_cast<double>(otherCount);
            std::cout << std::format("avg frame: IDR {:.0f} B, other {:.0f} B ({:.1f}x)\n", idrAvg, otherAvg,
                                     idrAvg / otherAvg);
        }
        if (!frames.empty())
        {
            const double seconds = static_cast<double>(frames.size()) / fps;
            std::cout << std::format("bitrate at {} fps: {:.0f} kbit/s\n", fps,
                                     static_cast<double>(stream.size()) * 8.0 / seconds / 1000.0);
        }

        std::cout << "\nfirst NAL units:\n";
        for (std::size_t i = 0; i < std::min<std::size_t>(units.size(), 12); ++i)
        {
            const pi_tracker::NalUnit &unit = units[i];
            std::cout << std::format("  offset {:7}  size {:6}  {:5} ref {}{}\n", unit.offset, unit.bytes.size(),
                                     pi_tracker::nalTypeName(unit.type()), unit.refIdc(),
                                     unit.startsFrame() ? "  <- new frame" : "");
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}