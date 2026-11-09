#include "drone_state.h"

#include <numbers>

namespace mav = pi_tracker::mavlink;

namespace {

double degrees(float radians) {
  return static_cast<double>(radians) * 180.0 / std::numbers::pi;
}

} // namespace

DroneState::DroneState(QObject *parent) : QObject{parent} {}

void DroneState::onHeartbeat(const mav::Heartbeat &heartbeat) {
  m_linkOk = true;
  m_mode = QString::fromUtf8(mav::copterModeName(heartbeat.customMode));
  m_armed = heartbeat.armed();
  emit heartbeatChanged(); // QML перечитає linkOk, mode і armed
}

void DroneState::onAttitude(const mav::Attitude &attitude) {
  m_rollDeg = degrees(attitude.roll);
  m_pitchDeg = degrees(attitude.pitch);
  m_yawDeg = degrees(attitude.yaw);
  emit attitudeChanged();
}

void DroneState::onPosition(const mav::GlobalPosition &position) {
  m_altitudeM = static_cast<double>(position.relativeAltM);
  emit positionChanged();
}

void DroneState::onLinkLost() {
  m_linkOk = false;
  emit heartbeatChanged();
}
