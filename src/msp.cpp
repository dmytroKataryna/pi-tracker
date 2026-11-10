#include "pi_tracker/msp.h"

#include "pi_tracker/mavlink.h" // readI16Le: той самий little-endian, що в MAVLink (День 22)

namespace pi_tracker::msp {

std::vector<std::uint8_t> encodeRequest(std::uint8_t command,
                                        std::span<const std::uint8_t> payload) {
  const auto size = static_cast<std::uint8_t>(payload.size());
  std::vector<std::uint8_t> frame{'$', 'M', '<', size, command};
  std::uint8_t checksum = size ^ command;
  for (const std::uint8_t byte : payload) {
    frame.push_back(byte);
    checksum ^= byte;
  }
  frame.push_back(checksum);
  return frame;
}

std::optional<Frame> Parser::feed(std::uint8_t byte) {
  switch (m_state) {
  case State::Dollar:
    if (byte == '$') {
      m_state = State::M;
    }
    break;

  case State::M:
    m_state = (byte == 'M') ? State::Direction : State::Dollar;
    break;

  case State::Direction:
    if (byte == '>' || byte == '!') {
      m_frame = Frame{};
      m_frame.isError = (byte == '!');
      m_state = State::Size;
    } else {
      m_state = State::Dollar; // '<' — це чужий запит, а не відповідь; решту
                               // відкидаємо
    }
    break;

  case State::Size:
    m_size = byte;
    m_checksum = byte;
    m_state = State::Command;
    break;

  case State::Command:
    m_frame.command = byte;
    m_checksum = m_checksum ^ byte;
    if (m_size == 0) {
      m_state = State::Checksum;
    } else {
      m_state = State::Payload;
    }
    break;

  case State::Payload:
    m_frame.payload.push_back(byte);
    m_checksum = m_checksum ^ byte;
    if (m_frame.payload.size() == m_size) {
      m_state = State::Checksum;
    }
    break;

  case State::Checksum:
    m_state = State::Dollar;
    if (m_checksum == byte) {
      return std::move(m_frame);
    } else {
      ++m_checksumErrors;
    }
    break;
  }
  return std::nullopt;
}

void Parser::reset() {
  m_frame = Frame{};
  m_state = State::Dollar;
  m_size = 0;
  m_checksum = 0;
}

namespace {

// Чи це успішна відповідь на потрібну команду з щонайменше minSize байтами
// даних
bool fits(const Frame &frame, std::uint8_t command, std::size_t minSize) {
  return !frame.isError && frame.command == command &&
         frame.payload.size() >= minSize;
}

} // namespace

std::optional<ApiVersion> decodeApiVersion(const Frame &frame) {
  if (!fits(frame, kApiVersion, 3)) {
    return std::nullopt;
  }
  return ApiVersion{frame.payload[0], frame.payload[1], frame.payload[2]};
}

std::optional<std::string> decodeFcVariant(const Frame &frame) {
  if (!fits(frame, kFcVariant, 4)) {
    return std::nullopt;
  }
  return std::string(frame.payload.begin(), frame.payload.begin() + 4);
}

std::optional<FcVersion> decodeFcVersion(const Frame &frame) {
  if (!fits(frame, kFcVersion, 3)) {
    return std::nullopt;
  }
  return FcVersion{frame.payload[0], frame.payload[1], frame.payload[2]};
}

std::optional<Attitude> decodeAttitude(const Frame &frame) {
  if (!fits(frame, kAttitude, 6)) {
    return std::nullopt;
  }
  Attitude attitude;
  attitude.rollDeg = mavlink::readI16Le(frame.payload, 0) / 10.0;
  attitude.pitchDeg = mavlink::readI16Le(frame.payload, 2) / 10.0;
  attitude.yawDeg = mavlink::readI16Le(frame.payload, 4);
  return attitude;
}

std::optional<RawImu> decodeRawImu(const Frame &frame) {
  if (!fits(frame, kRawImu, 18)) {
    return std::nullopt;
  }
  RawImu imu;
  for (std::size_t i = 0; i < 3; ++i) {
    imu.acc[i] = mavlink::readI16Le(frame.payload, i * 2);
    imu.gyro[i] = mavlink::readI16Le(frame.payload, 6 + i * 2);
    imu.mag[i] = mavlink::readI16Le(frame.payload, 12 + i * 2);
  }
  return imu;
}

} // namespace pi_tracker::msp
