#include "battery.h"

namespace drone {

float cellVoltage(float packVoltage, int cellCount) {
  return packVoltage / static_cast<float>(cellCount);
}

}  // namespace drone
