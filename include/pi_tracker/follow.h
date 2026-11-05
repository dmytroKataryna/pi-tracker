#pragma once

#include "pi_tracker/marker_detector.h"

#include <optional>

namespace pi_tracker
{

    // Калібрування лінійкою: маркер на відомій відстані мав такий розмір у пікселях.
    // Модель камери-обскури: розмір у пікселях ∝ 1 / відстань
    struct DistanceCalibration
    {
        float refDistanceCm{};
        float refSidePx{};
    };

    // Відстань до маркера. std::nullopt — порахувати не можна (нульовий розмір або калібрування не задано)
    std::optional<float> estimateDistanceCm(float sidePx, const DistanceCalibration &calibration);

    // Що ми попросили б у польотного контролера, щоб маркер був у центрі кадру на заданій відстані
    struct FollowCommand
    {
        float yawRateDegS{}; // + поворот праворуч
        float forwardMS{};   // + рух уперед
    };

    struct FollowConfig
    {
        float deadband{0.05f};           // |offset.x| менше — не повертаємо: це шум детектора
        float yawGainDegS{60.0f};        // offset.x = 1 (край кадру) → 60 °/с
        float maxYawRateDegS{30.0f};     // насичення: швидше не повертаємо
        float targetDistanceCm{100.0f};
        float forwardGainMSPerCm{0.01f}; // на 10 см далі за ціль → 0.1 м/с
        float maxForwardMS{0.3f};
    };

    // Пропорційний регулятор (P): команда пропорційна помилці, з мертвою зоною і насиченням
    FollowCommand followMarker(const Marker &marker, std::optional<float> distanceCm, const FollowConfig &config = {});

} // namespace pi_tracker
