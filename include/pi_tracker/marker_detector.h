#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>

namespace pi_tracker
{

    // Один знайдений ArUco-маркер
    struct Marker
    {
        int id{};
        std::array<cv::Point2f, 4> corners; // кути в пікселях: верхній лівий, далі за годинниковою стрілкою
        cv::Point2f center;                 // середнє чотирьох кутів
        float sidePx{};                     // середня довжина сторони: що більша, то ближче маркер
        cv::Point2f offset;                 // відхилення центру від центру кадру: −1…+1 по x і по y
    };

    // Шукає маркери словника DICT_4X4_50 (сітка 4×4, номери 0…49) на сірому зображенні
    class MarkerDetector
    {
    public:
        MarkerDetector();

        // gray — 8-бітне сіре зображення, наприклад площина Y кадру I420. Пікселі не змінює
        std::vector<Marker> detect(const cv::Mat &gray) const;

        // Той самий словник, щоб згенерувати маркер для друку чи екрана
        static cv::aruco::Dictionary dictionary();

    private:
        cv::aruco::ArucoDetector m_detector;
    };

    // Заголовок cv::Mat поверх ЧУЖОЇ пам'яті: без копіювання, Mat пікселями не володіє.
    // Mat дійсний, лише поки живе той, хто володіє pixels
    cv::Mat wrapGray(std::span<const std::uint8_t> pixels, int width, int height);

} // namespace pi_tracker
