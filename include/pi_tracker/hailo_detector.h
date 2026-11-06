#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <hailo/hailort.hpp>

namespace pi_tracker
{

    // HailoRT не кидає винятків: кожна функція повертає Expected<T> (значення АБО код помилки).
    // На межі з нашим кодом перетворюємо помилку на виняток — як з GError на Дні 18.
    // Шаблон живе в заголовку: компілятор має бачити тіло, щоб згенерувати версію для кожного T
    template <typename T>
    T orThrow(hailort::Expected<T> expected, std::string_view what)
    {
        if (!expected)
        {
            auto desc = hailo_get_status_message(expected.status());
            auto code = static_cast<int>(expected.status());
            std::string error_msg = std::string(what) + ": " + desc + " (code: " + std::to_string(code) + ")";
            throw std::runtime_error(error_msg);
        }
        return expected.release();
    }

    // Те саме для функцій, що повертають лише статус
    void check(hailo_status status, std::string_view what);

    // Один знайдений об'єкт. Координати — частки кадру 0…1, тож не залежать від розміру зображення
    struct Detection
    {
        int classId{};
        float score{};
        float xMin{};
        float yMin{};
        float xMax{};
        float yMax{};
    };

    // Розбирає вихід NMS «по класах»: для кожного класу — float кількість, далі стільки рамок по 5 float
    // (y_min, x_min, y_max, x_max, score). Рамки з score < minScore пропускає
    std::vector<Detection> parseNmsByClass(std::span<const float> output, int classCount, float minScore);

    // "person", "car", ... — 80 класів COCO, на яких навчено YOLO
    std::string_view cocoName(int classId);

    // YOLO на Hailo: кадр RGB потрібного розміру → список об'єктів
    class HailoDetector
    {
    public:
        explicit HailoDetector(const std::string &hefPath);

        HailoDetector(const HailoDetector &) = delete;
        HailoDetector &operator=(const HailoDetector &) = delete;

        int inputWidth() const { return m_inputWidth; }
        int inputHeight() const { return m_inputHeight; }

        // rgb — inputWidth × inputHeight × 3 байти, рядок за рядком
        std::vector<Detection> detect(std::span<const std::uint8_t> rgb, float minScore = 0.5f);

    private:
        std::unique_ptr<hailort::VDevice> m_device;         // чип; має жити довше за модель
        std::shared_ptr<hailort::InferModel> m_model;       // модель з .hef
        hailort::ConfiguredInferModel m_configured;         // модель, завантажена в чип
        hailort::ConfiguredInferModel::Bindings m_bindings; // куди класти вхід і звідки брати вихід
        std::vector<std::uint8_t> m_input;
        std::vector<float> m_output; // float, а не байти: так вирівнювання правильне
        int m_inputWidth{};
        int m_inputHeight{};
        int m_classCount{};
    };

} // namespace pi_tracker
