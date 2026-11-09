cmake_minimum_required(VERSION 3.21)
project(pi_tracker_gcs LANGUAGES CXX)

# Окремий проєкт: наземна станція збирається на Mac з Qt, без libcamera, GStreamer та інших залежностей Pi
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# moc (meta-object compiler) — генератор коду Qt: читає класи з Q_OBJECT і дописує код для сигналів і слотів
set(CMAKE_AUTOMOC ON)

# Quick — модуль для QML-вікон (Qt-день 2)
find_package(Qt6 REQUIRED COMPONENTS Core Network Quick)
# Стандартні налаштування Qt для QML-модулів (політики CMake, AUTOMOC тощо)
qt_standard_project_setup(REQUIRES 6.5)

# Qt-день 1: станція в терміналі
add_executable(gcs_console
    console_main.cpp
    telemetry_link.cpp
    ../src/mavlink.cpp  # той самий розбір MAVLink, що на Pi (День 22)
)
target_include_directories(gcs_console PRIVATE ../include)
target_link_libraries(gcs_console PRIVATE Qt6::Core Qt6::Network)
target_compile_options(gcs_console PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

# Qt-день 2: станція з вікном на QML
qt_add_executable(gcs_window
    window_main.cpp
    telemetry_link.cpp
    ../src/mavlink.cpp
)
# QML-модуль «Gcs»: Main.qml і C++-тип DroneState (QML_ELEMENT) стають доступні через `import Gcs`
qt_add_qml_module(gcs_window
    URI Gcs
    QML_FILES Main.qml
    SOURCES drone_state.h drone_state.cpp
)
target_include_directories(gcs_window PRIVATE ../include)
target_link_libraries(gcs_window PRIVATE Qt6::Quick Qt6::Network)
target_compile_options(gcs_window PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
