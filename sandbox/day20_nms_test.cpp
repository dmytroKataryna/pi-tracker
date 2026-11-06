#include "pi_tracker/hailo_detector.h"

#include <cmath>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <stdexcept>
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

    bool near(float a, float b)
    {
        return std::abs(a - b) < 1e-6f;
    }

    // Виняток, якого тест не чекав (наприклад, з незробленого TODO), — це FAIL, а не падіння всієї програми
    template <typename Test>
    void runCase(const std::string &name, Test test)
    {
        try
        {
            test();
        }
        catch (const std::exception &e)
        {
            check(false, std::format("{}: unexpected exception: {}", name, e.what()));
        }
    }

} // namespace

int main()
{
    // 1. orThrow: значення — повертає його (і не копіює: unique_ptr копіювати не можна взагалі)
    runCase("orThrow value", [] {
        hailort::Expected<std::unique_ptr<int>> ok{std::make_unique<int>(42)};
        const std::unique_ptr<int> value = pi_tracker::orThrow(std::move(ok), "ok");
        check(value && *value == 42, "orThrow: must return the value from a successful Expected");
    });

    // 2. orThrow: помилка — виняток з текстом і кодом
    runCase("orThrow error", [] {
        hailort::Expected<int> failed{hailort::make_unexpected(HAILO_HEF_NOT_COMPATIBLE_WITH_DEVICE)};
        std::string message;
        try
        {
            (void)pi_tracker::orThrow(std::move(failed), "load model");
        }
        catch (const std::runtime_error &e)
        {
            message = e.what();
        }
        check(!message.empty(), "orThrow: must throw std::runtime_error for an error status");
        check(message.find("load model") != std::string::npos, std::format("orThrow: message '{}' must contain 'load model'", message));
        check(message.find("93") != std::string::npos, std::format("orThrow: message '{}' must contain the code 93", message));
    });

    // 3. parseNmsByClass: 3 класи. Клас 0 — одна рамка, клас 1 — нічого, клас 2 — дві (одна слабка)
    runCase("nms parse", [] {
        const std::vector<float> output{
            1.0f, 0.10f, 0.20f, 0.50f, 0.60f, 0.90f,  // клас 0 (person): y_min x_min y_max x_max score
            0.0f,                                     // клас 1: рамок немає
            2.0f, 0.00f, 0.00f, 1.00f, 1.00f, 0.30f,  // клас 2: слабка, нижче порогу
                  0.40f, 0.30f, 0.80f, 0.70f, 0.75f,  //          і сильна
        };
        const auto detections = pi_tracker::parseNmsByClass(output, 3, 0.5f);
        check(detections.size() == 2, std::format("nms: {} detections, expected 2", detections.size()));
        if (detections.size() == 2)
        {
            const pi_tracker::Detection &a = detections[0];
            check(a.classId == 0 && near(a.score, 0.90f), std::format("nms: first is class {} score {:.2f}, expected 0 / 0.90", a.classId, a.score));
            check(near(a.xMin, 0.20f) && near(a.yMin, 0.10f) && near(a.xMax, 0.60f) && near(a.yMax, 0.50f),
                  std::format("nms: first box x {:.2f}..{:.2f} y {:.2f}..{:.2f}, expected x 0.20..0.60 y 0.10..0.50 "
                              "(the order in memory is y_min, x_min, y_max, x_max)",
                              a.xMin, a.xMax, a.yMin, a.yMax));
            const pi_tracker::Detection &b = detections[1];
            check(b.classId == 2 && near(b.score, 0.75f), std::format("nms: second is class {} score {:.2f}, expected 2 / 0.75", b.classId, b.score));
            check(near(b.xMin, 0.30f) && near(b.yMax, 0.80f), std::format("nms: second box x_min {:.2f} y_max {:.2f}, expected 0.30 / 0.80", b.xMin, b.yMax));
        }
    });

    // 4. parseNmsByClass: пошкоджений вихід (клас обіцяє 5 рамок, а даних немає) — виняток, а не читання за межами
    runCase("nms broken", [] {
        const std::vector<float> broken{5.0f, 0.1f, 0.2f};
        bool threw = false;
        try
        {
            (void)pi_tracker::parseNmsByClass(broken, 1, 0.5f);
        }
        catch (const std::out_of_range &)
        {
            threw = true;
        }
        check(threw, "nms: a class claiming more boxes than the buffer holds must throw std::out_of_range");
    });

    if (g_failures == 0)
    {
        std::cout << "all tests passed\n";
        return EXIT_SUCCESS;
    }
    std::cout << std::format("{} check(s) failed\n", g_failures);
    return EXIT_FAILURE;
}
