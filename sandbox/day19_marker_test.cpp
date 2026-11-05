#include "pi_tracker/marker_detector.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace
{

    int g_failures = 0;

    void check(bool ok, const std::string &message)
    {
        if (!ok)
        {
            ++g_failures;
            std::cout << "FAIL " << message << '\n';
        }
    }

    // Сірий кадр 1280×720 з маркером id розміром side, лівий верхній кут — (x, y)
    std::vector<std::uint8_t> frameWithMarker(int id, int side, int x, int y)
    {
        std::vector<std::uint8_t> pixels(1280 * 720, 200); // світлий фон: маркеру потрібна біла рамка навколо
        cv::Mat frame{720, 1280, CV_8UC1, pixels.data()};  // тут пишемо, тому не wrapGray (той лише для читання)
        cv::Mat marker;
        cv::aruco::generateImageMarker(pi_tracker::MarkerDetector::dictionary(), id, side, marker);
        marker.copyTo(frame(cv::Rect{x, y, side, side})); // пише прямо в pixels: frame — лише вікно в них
        return pixels;
    }

} // namespace

int main()
{
    const pi_tracker::MarkerDetector detector;

    // 1. wrapGray не копіює: Mat дивиться в ту саму пам'ять
    std::vector<std::uint8_t> pixels(1280 * 720, 0);
    const cv::Mat view = pi_tracker::wrapGray(pixels, 1280, 720);
    check(view.data == pixels.data(), "wrapGray: Mat must point into the same memory (no copy)");
    check(view.rows == 720 && view.cols == 1280 && view.type() == CV_8UC1,
          std::format("wrapGray: Mat is {} rows x {} cols, expected 720 x 1280, CV_8UC1", view.rows, view.cols));
    if (g_failures > 0)
    {
        std::cout << "wrapGray is not ready (TODO 1): the other tests need it, stopping here\n";
        return EXIT_FAILURE;
    }
    pixels[0] = 77;
    check(view.at<std::uint8_t>(0, 0) == 77, "wrapGray: a change in pixels must be visible through the Mat");

    // 2. Порожній кадр — жодного маркера
    check(detector.detect(view).empty(), "empty frame: no markers");

    // 3. Маркер 7 по центру: 200×200 з лівим верхнім кутом (540, 260) → центр (640, 360), зсув (0, 0)
    {
        const std::vector<std::uint8_t> frame = frameWithMarker(7, 200, 540, 260);
        const auto markers = detector.detect(pi_tracker::wrapGray(frame, 1280, 720));
        check(markers.size() == 1, std::format("center: {} markers, expected 1", markers.size()));
        if (markers.size() == 1)
        {
            const pi_tracker::Marker &m = markers[0];
            check(m.id == 7, std::format("center: id {}, expected 7", m.id));
            check(std::abs(m.center.x - 640.0f) < 1.5f && std::abs(m.center.y - 360.0f) < 1.5f,
                  std::format("center: center ({:.1f}, {:.1f}), expected (640, 360)", m.center.x, m.center.y));
            check(std::abs(m.sidePx - 200.0f) < 3.0f, std::format("center: side {:.1f}, expected ~200", m.sidePx));
            check(std::abs(m.offset.x) < 0.01f && std::abs(m.offset.y) < 0.01f,
                  std::format("center: offset ({:.3f}, {:.3f}), expected (0, 0)", m.offset.x, m.offset.y));
        }
    }

    // 4. Маркер 3 праворуч угорі: центр (1100, 160) → зсув x = (1100−640)/640 ≈ +0.719, y = (160−360)/360 ≈ −0.556
    {
        const std::vector<std::uint8_t> frame = frameWithMarker(3, 120, 1040, 100);
        const auto markers = detector.detect(pi_tracker::wrapGray(frame, 1280, 720));
        check(markers.size() == 1, std::format("corner: {} markers, expected 1", markers.size()));
        if (markers.size() == 1)
        {
            const pi_tracker::Marker &m = markers[0];
            check(m.id == 3, std::format("corner: id {}, expected 3", m.id));
            check(std::abs(m.offset.x - 0.719f) < 0.01f && std::abs(m.offset.y + 0.556f) < 0.01f,
                  std::format("corner: offset ({:.3f}, {:.3f}), expected (+0.719, -0.556)", m.offset.x, m.offset.y));
        }
    }

    // 5. Неправильні розміри: wrapGray має кинути виняток, а не читати чужу пам'ять
    {
        bool threw = false;
        try
        {
            const std::vector<std::uint8_t> small(100);
            (void)pi_tracker::wrapGray(small, 1280, 720);
        }
        catch (const std::invalid_argument &)
        {
            threw = true;
        }
        check(threw, "wrapGray: too few bytes must throw std::invalid_argument");
    }

    if (g_failures == 0)
    {
        std::cout << "all tests passed\n";
        return EXIT_SUCCESS;
    }
    std::cout << std::format("{} check(s) failed\n", g_failures);
    return EXIT_FAILURE;
}
