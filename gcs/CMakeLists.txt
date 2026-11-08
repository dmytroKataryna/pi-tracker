cmake_minimum_required(VERSION 3.21)
project(pi_tracker_gcs LANGUAGES CXX)

# Окремий проєкт: наземна станція збирається на Mac з Qt, без libcamera, GStreamer та інших залежностей Pi
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# moc (meta-object compiler) — генератор коду Qt: читає класи з Q_OBJECT і дописує код для сигналів і слотів
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_executable(gcs_console
    console_main.cpp
    telemetry_link.cpp
    ../src/mavlink.cpp  # той самий розбір MAVLink, що на Pi (День 22)
)
target_include_directories(gcs_console PRIVATE ../include)
target_link_libraries(gcs_console PRIVATE Qt6::Core Qt6::Network)
target_compile_options(gcs_console PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
