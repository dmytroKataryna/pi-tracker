#include "pi_tracker/marker_detector.h"

#include <opencv2/imgcodecs.hpp>

#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

// Генерує маркер для екрана чи друку: ./day19_marker_gen [id] → data/marker_<id>.png
int main(int argc, char *argv[])
{
    const int id = argc > 1 ? std::stoi(argv[1]) : 7;

    cv::Mat marker;
    cv::aruco::generateImageMarker(pi_tracker::MarkerDetector::dictionary(), id, 600, marker);

    // Біла рамка обов'язкова: детектор шукає чорний квадрат на світлому тлі
    cv::Mat withBorder;
    cv::copyMakeBorder(marker, withBorder, 100, 100, 100, 100, cv::BORDER_CONSTANT, cv::Scalar{255});

    const std::string path = std::format("data/marker_{}.png", id);
    if (!cv::imwrite(path, withBorder))
    {
        std::cerr << "cannot write " << path << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "saved " << path << '\n';
}
