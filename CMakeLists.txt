cmake_minimum_required(VERSION 3.15)
project(FaceGate)

# 默认 Release（MinGW Makefiles 留空 = -O0）
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_COMPILER "F:/Apps/Qt5.15.2/Tools/mingw810_64/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "F:/Apps/Qt5.15.2/Tools/mingw810_64/bin/g++.exe")
set(CMAKE_MAKE_PROGRAM "F:/Apps/Qt5.15.2/Tools/mingw810_64/bin/mingw32-make.exe")

set(Qt5_ROOT "F:/Apps/Qt5.15.2/5.15.2/mingw81_64")
set(CMAKE_PREFIX_PATH "${Qt5_ROOT}" ${CMAKE_PREFIX_PATH})

set(OpenCV_DIR "F:/CODE/Qt_forNoVS/SmartSurveillance/utils/opencv4_5_2/x64/mingw/lib")

find_package(Qt5 COMPONENTS Core Gui Widgets Concurrent SerialPort REQUIRED)
find_package(OpenCV REQUIRED
  COMPONENTS core imgproc video videoio objdetect)

# 可插拔人脸算法库（与监控项目共用同一份源码，改算法两边同步生效）
set(FACEALGORITHM_DIR "${CMAKE_SOURCE_DIR}/../FaceAlgorithm" CACHE PATH "FaceAlgorithm 源码目录")
add_subdirectory(${FACEALGORITHM_DIR} ${CMAKE_BINARY_DIR}/FaceAlgorithm)

add_subdirectory(src)

file(COPY resources/ DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

message(STATUS "Qt5 version: ${Qt5_VERSION}")
message(STATUS "OpenCV found: ${OpenCV_VERSION}")
message(STATUS "Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
