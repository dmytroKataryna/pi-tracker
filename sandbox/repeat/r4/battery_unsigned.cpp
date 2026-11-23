#include "battery.h"

namespace drone {

// Колега вирішив, що кількість елементів не буває від'ємною, і змінив тип на unsigned.
// Але лише тут, у .cpp. Заголовок battery.h він не чіпав.
float cellVoltage(float packVoltage, unsigned cellCount) {
  return packVoltage / static_cast<float>(cellCount);
}

}  // namespace drone
