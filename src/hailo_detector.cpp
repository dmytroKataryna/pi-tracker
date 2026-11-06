#include "pi_tracker/hailo_detector.h"

#include <array>
#include <chrono>
#include <cstring>

namespace pi_tracker
{

    namespace
    {
        constexpr std::array<std::string_view, 80> kCocoNames{
            "person", "bicycle", "car", "motorcycle", "airplane", "bus",
            "train", "truck", "boat", "traffic light", "fire hydrant", "stop sign",
            "parking meter", "bench", "bird", "cat", "dog", "horse",
            "sheep", "cow", "elephant", "bear", "zebra", "giraffe",
            "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
            "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
            "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
            "fork", "knife", "spoon", "bowl", "banana", "apple",
            "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza",
            "donut", "cake", "chair", "couch", "potted plant", "bed",
            "dining table", "toilet", "tv", "laptop", "mouse", "remote",
            "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
            "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
            "hair drier", "toothbrush"};

        constexpr std::size_t kBoxFloats = 5; // y_min, x_min, y_max, x_max, score
    } // namespace

    void check(hailo_status status, std::string_view what)
    {
        if (status != HAILO_SUCCESS)
        {
            throw std::runtime_error(
                std::format("{}: {} ({})", what, hailo_get_status_message(status), static_cast<int>(status)));
        }
    }

    std::string_view cocoName(int classId)
    {
        if (classId < 0 || static_cast<std::size_t>(classId) >= kCocoNames.size())
        {
            return "unknown";
        }
        return kCocoNames[static_cast<std::size_t>(classId)];
    }

    std::vector<Detection> parseNmsByClass(std::span<const float> output, int classCount, float minScore)
    {
        std::vector<Detection> detections;
        std::size_t pos = 0;
        for (int classId = 0; classId < classCount; classId++)
        {
            if (pos >= output.size())
            {
                throw std::out_of_range("parseNmsByClass: output ends before the last class");
            }
            const auto count = static_cast<std::size_t>(output[pos]);
            ++pos;

            const std::size_t end = pos + count * kBoxFloats;
            if (end > output.size())
            {
                throw std::out_of_range("parseNmsByClass: boxes go past the end");
            }

            for (std::size_t k = 0; k < count; ++k)
            {
                const std::span<const float> box = output.subspan(pos, kBoxFloats); // 5 чисел, починаючи з pos
                pos += kBoxFloats;
                if (box[4] < minScore)
                {
                    continue;
                }
                // Detection{classId, score, xMin, yMin, xMax, yMax}: порядок у пам'яті інший, ніж у Detection
                detections.push_back(Detection{classId, box[4], box[1], box[0], box[3], box[2]});
            }
        }

        return detections;
    }

    HailoDetector::HailoDetector(const std::string &hefPath)
    {
        m_device = orThrow(hailort::VDevice::create(), "VDevice::create (is /dev/hailo0 there?)");
        m_model = orThrow(m_device->create_infer_model(hefPath), "create_infer_model " + hefPath);

        const hailo_3d_image_shape_t shape = m_model->inputs().at(0).shape();
        m_inputHeight = static_cast<int>(shape.height);
        m_inputWidth = static_cast<int>(shape.width);
        m_classCount = static_cast<int>(m_model->outputs().at(0).get_nms_shape().value().number_of_classes);

        m_configured = orThrow(m_model->configure(), "InferModel::configure");
        m_bindings = orThrow(m_configured.create_bindings(), "create_bindings");

        m_input.resize(m_model->input()->get_frame_size());
        m_output.resize(m_model->output()->get_frame_size() / sizeof(float));
        check(orThrow(m_bindings.input(), "bindings.input")
                  .set_buffer(hailort::MemoryView(m_input.data(), m_input.size())),
              "input set_buffer");
        check(orThrow(m_bindings.output(), "bindings.output")
                  .set_buffer(hailort::MemoryView(m_output.data(), m_output.size() * sizeof(float))),
              "output set_buffer");
    }

    std::vector<Detection> HailoDetector::detect(std::span<const std::uint8_t> rgb, float minScore)
    {
        if (rgb.size() != m_input.size())
        {
            throw std::invalid_argument(
                std::format("HailoDetector::detect: got {} bytes, model wants {}", rgb.size(), m_input.size()));
        }
        std::memcpy(m_input.data(), rgb.data(), rgb.size());
        check(m_configured.run(m_bindings, std::chrono::milliseconds{1000}), "ConfiguredInferModel::run");
        return parseNmsByClass(m_output, m_classCount, minScore);
    }

} // namespace pi_tracker
