cmake_minimum_required(VERSION 3.20)
project(pi_tracker LANGUAGES CXX)
find_package(Threads REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic -Wconversion)

add_library(pitracker
    src/fd.cpp
    src/i2c_device.cpp
    src/imu.cpp
    src/mapped_buffer.cpp
    src/serial_port.cpp
    src/tf_luna.cpp
    src/udp_sender.cpp
    src/yuv420.cpp
    src/h264_nal.cpp
)

target_link_libraries(pitracker PUBLIC Threads::Threads)
target_include_directories(pitracker PUBLIC include)

# libcamera знаходимо через pkg-config. Окрема бібліотека, щоб sensord не залежав від камери
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCAMERA REQUIRED IMPORTED_TARGET libcamera)

add_library(pitracker_camera src/camera_capture.cpp)
target_link_libraries(pitracker_camera PUBLIC pitracker PkgConfig::LIBCAMERA)

add_executable(hello src/main.cpp)

add_executable(sensord apps/sensord.cpp)
target_link_libraries(sensord PRIVATE pitracker)

# GStreamer: окрема бібліотека, щоб sensord і камера не залежали від нього
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0)
add_library(pitracker_video src/h264_streamer.cpp)


file(GLOB SANDBOX_SOURCES CONFIGURE_DEPENDS sandbox/*.cpp)
foreach(src ${SANDBOX_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE pitracker)
endforeach()

target_sources(ch2_8_files PRIVATE sandbox/helper/ch2_8_get_integer.cpp)
target_link_libraries(day16_capture PRIVATE pitracker_camera)

install(TARGETS sensord RUNTIME DESTINATION bin)

target_link_libraries(pitracker_video PUBLIC pitracker PkgConfig::GST)
target_link_libraries(day18_stream PRIVATE pitracker_camera pitracker_video)

# OpenCV: окрема бібліотека, щоб решта проєкту від нього не залежала
find_package(OpenCV REQUIRED COMPONENTS core imgproc objdetect imgcodecs)
add_library(pitracker_vision src/marker_detector.cpp src/follow.cpp)
target_link_libraries(pitracker_vision PUBLIC pitracker ${OpenCV_LIBS})
target_link_libraries(day19_marker_test PRIVATE pitracker_vision)
target_link_libraries(day19_marker_gen PRIVATE pitracker_vision)
target_link_libraries(day19_aruco PRIVATE pitracker_camera pitracker_vision)
target_link_libraries(day19_follow_test PRIVATE pitracker_vision)
