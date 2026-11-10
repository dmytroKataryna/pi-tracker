cmake_minimum_required(VERSION 3.25) # 3.25: SYSTEM у FetchContent_Declare
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


include(FetchContent)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    SYSTEM # попередження в заголовках GoogleTest — не наша справа
)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(unit_tests ../tests/msp_test.cpp)
# gtest_main дає готовий main(), який запускає всі TEST
target_link_libraries(unit_tests PRIVATE pitracker_portable GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(unit_tests) # кожен TEST стає окремим тестом для ctest
