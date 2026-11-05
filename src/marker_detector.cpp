#include "pi_tracker/marker_detector.h"

#include <format>
#include <stdexcept>

namespace pi_tracker
{

    MarkerDetector::MarkerDetector() : m_detector{dictionary(), cv::aruco::DetectorParameters{}}
    {
    }

    cv::aruco::Dictionary MarkerDetector::dictionary()
    {
        return cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
    }

    std::vector<Marker> MarkerDetector::detect(const cv::Mat &gray) const
    {
        if (gray.empty() || gray.type() != CV_8UC1)
        {
            throw std::invalid_argument("MarkerDetector::detect: expected a non-empty 8-bit gray image");
        }

        // OpenCV повертає паралельні масиви: corners[i] належать ids[i]
        std::vector<std::vector<cv::Point2f>> corners;
        std::vector<int> ids;
        m_detector.detectMarkers(gray, corners, ids);

        const float halfWidth = static_cast<float>(gray.cols) / 2.0f;
        const float halfHeight = static_cast<float>(gray.rows) / 2.0f;

        std::vector<Marker> markers;
        markers.reserve(ids.size());
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            Marker marker;
            marker.id = ids[i];
            for (std::size_t k = 0; k < 4; ++k)
            {
                marker.corners[k] = corners[i][k];
            }

            cv::Point2f center = (marker.corners[0] + marker.corners[1] + marker.corners[2] + marker.corners[3]) / 4.0f;

            double perimeter = 0.0;
            for (std::size_t k = 0; k < 4; ++k)
            {
                auto a = marker.corners[k];
                auto b = marker.corners[(k + 1) % 4];
                perimeter += norm(b - a);
            }
            float sidePx = static_cast<float>(perimeter / 4.0);

            auto x = (center.x - halfWidth) / halfWidth;
            auto y = (center.y - halfHeight) / halfHeight;
            cv::Point2f offset = cv::Point2f(x, y);

            marker.center = center;
            marker.sidePx = sidePx;
            marker.offset = offset;

            markers.push_back(marker);
        }
        return markers;
    }

    cv::Mat wrapGray(std::span<const std::uint8_t> pixels, int width, int height)
    {
        if (width <= 0 || height <= 0 ||
            pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
        {
            throw std::invalid_argument(std::format("wrapGray: {} bytes is not a {}x{} image", pixels.size(), width, height));
        }

        return cv::Mat{height, width, CV_8UC1, const_cast<std::uint8_t *>(pixels.data())};
    }

} // namespace pi_tracker
