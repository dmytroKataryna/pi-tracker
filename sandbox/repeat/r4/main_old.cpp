// Старий код: написаний ще до того, як у cellVoltage з'явився параметр cellCount.
#include "battery.h"

#include <cstdio>

int main() {
  const float cell = drone::cellVoltage(16.8f);
  std::printf("cell voltage: %.2f V\n", static_cast<double>(cell));
}
