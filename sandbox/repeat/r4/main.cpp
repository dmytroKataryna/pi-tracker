#include "battery.h"

#include <cstdio>

int main() {
  const float cell = drone::cellVoltage(16.8f, 4);  // повністю заряджена 4S-батарея
  std::printf("cell voltage: %.2f V\n", static_cast<double>(cell));
}
