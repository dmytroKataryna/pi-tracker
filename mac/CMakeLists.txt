cmake_minimum_required(VERSION 3.21)
project(pi_tracker_mac LANGUAGES CXX)

# Переносимі частини pi-tracker, які збираються й на macOS: без libcamera, GStreamer, I2C і Hailo.
# Основний проєкт (../CMakeLists.txt) як і раніше збирається лише на Pi
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -Wpedantic -Wconversion)

add_library(pitracker_portable STATIC
    ../src/fd.cpp
    ../src/serial_port.cpp
    ../src/mavlink.cpp
    ../src/msp.cpp
)
target_include_directories(pitracker_portable PUBLIC ../include)

# День 23: MSP до польотного контролера
add_executable(day23_msp_test ../sandbox/day23_msp_test.cpp)
target_link_libraries(day23_msp_test PRIVATE pitracker_portable)
add_executable(day23_msp ../sandbox/day23_msp.cpp)
target_link_libraries(day23_msp PRIVATE pitracker_portable)
