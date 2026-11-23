#pragma once

namespace drone {

// Напруга одного елемента акумулятора = напруга всієї батареї / кількість елементів.
float cellVoltage(float packVoltage, int cellCount);

}  // namespace drone
