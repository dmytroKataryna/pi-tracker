#include "pi_tracker/follow.h"

#include <cmath>
#include <cstdlib>
#include <format>
#include <iostream>
#include <optional>
#include <string>

namespace
{

    int g_failures = 0;

    void expectNear(float actual, float expected, const std::string &what)
    {
        if (std::abs(actual - expected) > 0.001f)
        {
            ++g_failures;
            std::cout << std::format("FAIL {}: {:.3f}, expected {:.3f}\n", what, actual, expected);
        }
    }

    void expectDistance(std::optional<float> actual, std::optional<float> expected, const std::string &what)
    {
        if (actual.has_value() != expected.has_value())
        {
            ++g_failures;
            std::cout << std::format("FAIL {}: {}, expected {}\n", what, actual ? "a value" : "nullopt",
                                     expected ? "a value" : "nullopt");
            return;
        }
        if (actual)
        {
            expectNear(*actual, *expected, what);
        }
    }

    pi_tracker::Marker markerAt(float offsetX)
    {
        pi_tracker::Marker marker;
        marker.offset = {offsetX, 0.0f};
        return marker;
    }

} // namespace

int main()
{
    // Відстань: маркер на 50 см мав 300 px
    const pi_tracker::DistanceCalibration calibration{50.0f, 300.0f};
    expectDistance(pi_tracker::estimateDistanceCm(300.0f, calibration), 50.0f, "distance at the reference size");
    expectDistance(pi_tracker::estimateDistanceCm(150.0f, calibration), 100.0f, "half the size = twice as far");
    expectDistance(pi_tracker::estimateDistanceCm(600.0f, calibration), 25.0f, "twice the size = half as far");
    expectDistance(pi_tracker::estimateDistanceCm(0.0f, calibration), std::nullopt, "side 0 px");
    expectDistance(pi_tracker::estimateDistanceCm(-5.0f, calibration), std::nullopt, "negative side");
    expectDistance(pi_tracker::estimateDistanceCm(300.0f, {}), std::nullopt, "no calibration");

    // Поворот (налаштування за замовчуванням: мертва зона 0.05, 60 °/с на край кадру, не більше 30 °/с)
    expectNear(pi_tracker::followMarker(markerAt(0.02f), std::nullopt).yawRateDegS, 0.0f, "yaw inside the deadband");
    expectNear(pi_tracker::followMarker(markerAt(-0.04f), std::nullopt).yawRateDegS, 0.0f, "yaw inside the deadband (left)");
    expectNear(pi_tracker::followMarker(markerAt(0.25f), std::nullopt).yawRateDegS, 15.0f, "yaw: marker right of center");
    expectNear(pi_tracker::followMarker(markerAt(-0.25f), std::nullopt).yawRateDegS, -15.0f, "yaw: marker left of center");
    expectNear(pi_tracker::followMarker(markerAt(0.9f), std::nullopt).yawRateDegS, 30.0f, "yaw saturates at +30");
    expectNear(pi_tracker::followMarker(markerAt(-0.9f), std::nullopt).yawRateDegS, -30.0f, "yaw saturates at -30");

    // Рух уперед (ціль 100 см, 0.01 м/с на см, не більше 0.3 м/с)
    expectNear(pi_tracker::followMarker(markerAt(0.0f), 100.0f).forwardMS, 0.0f, "forward at the target distance");
    expectNear(pi_tracker::followMarker(markerAt(0.0f), 120.0f).forwardMS, 0.2f, "forward: 20 cm too far");
    expectNear(pi_tracker::followMarker(markerAt(0.0f), 80.0f).forwardMS, -0.2f, "forward: 20 cm too close");
    expectNear(pi_tracker::followMarker(markerAt(0.0f), 250.0f).forwardMS, 0.3f, "forward saturates at +0.3");
    expectNear(pi_tracker::followMarker(markerAt(0.0f), std::nullopt).forwardMS, 0.0f, "forward without distance");

    if (g_failures == 0)
    {
        std::cout << "all tests passed\n";
        return EXIT_SUCCESS;
    }
    std::cout << std::format("{} check(s) failed\n", g_failures);
    return EXIT_FAILURE;
}
