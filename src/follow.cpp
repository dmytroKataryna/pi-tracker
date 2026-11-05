#include "pi_tracker/follow.h"

#include <algorithm>
#include <cmath>

namespace pi_tracker
{

    std::optional<float> estimateDistanceCm(float sidePx, const DistanceCalibration &calibration)
    {
        if (sidePx <= 0 || calibration.refSidePx <= 0 || calibration.refDistanceCm <= 0)
        {
            return std::nullopt;
        }

        return calibration.refSidePx * calibration.refDistanceCm / sidePx;
    }

    FollowCommand followMarker(const Marker &marker, std::optional<float> distanceCm, const FollowConfig &config)
    {
        float yawRateDegS = 0.0;
        if (std::abs(marker.offset.x) < config.deadband)
        {
            yawRateDegS = 0.0;
        }
        else
        {
            yawRateDegS = std::clamp(config.yawGainDegS * marker.offset.x, -config.maxYawRateDegS, config.maxYawRateDegS);
        }

        float forwardMS = 0.0;
        if (distanceCm)
        {
            const float errorCm = *distanceCm - config.targetDistanceCm;
            forwardMS = std::clamp(config.forwardGainMSPerCm * errorCm, -config.maxForwardMS, config.maxForwardMS);
        }
        else
        {
            forwardMS = 0.0;
        }

        return FollowCommand{yawRateDegS, forwardMS};
    }

} // namespace pi_tracker
