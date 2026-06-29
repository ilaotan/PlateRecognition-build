# SPDX-License-Identifier: MIT
# Standalone NDK build for `libhyperlpr3.so` that replaces the legacy
# tensorrt/CUDA-based implementation. Targets the same Java JNI surface
# (`com.hyperai.hyperlpr3.core.HyperLPRCore`) and is consumed by
# hyperlpr3-android-sdk.
#
# This file is meant to be invoked from an Android Gradle externalNativeBuild
# block, but it also works as a plain CMake project for building .so
# artefacts from CI.

cmake_minimum_required(VERSION 3.22.1)

# Suppress the NDK toolchain's <3.10 deprecation noise that pollutes CI
# logs. 3.22.1 matches the version Gradle uses to drive our AAR build.
if(POLICY CMP0094)
    cmake_policy(SET CMP0094 NEW)  # find_package() uses upper-case <PACKAGEROOT>
endif()

project(hyperlpr3_ncnn LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output to a predictable location: ${CMAKE_BINARY_DIR}/lib/<abi>/libhyperlpr3.so
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/lib/${ANDROID_ABI}"
    CACHE INTERNAL "" FORCE)

# ---- OpenCV (Android package) --------------------------------------------
# The Android library module ships an OpenCV AAR; when this file is
# invoked standalone, the CI workflow provides OpenCV headers/libs via
# $OPENCV_ANDROID path. We delegate the layout discovery to OpenCV's
# own CMake config file (works for OpenCV 4.5+ and 4.10+):
#   sdk/native/jni/abi-<abi>/OpenCVConfig.cmake
# which exports OPENCV_LIBS for the current ANDROID_ABI. The abi
# directory is present in every officially published OpenCV Android
# SDK we care about.

if(DEFINED ENV{OPENCV_ANDROID})
  set(OPENCV_ANDROID $ENV{OPENCV_ANDROID})
endif()

message(STATUS "OPENCV_ANDROID = '${OPENCV_ANDROID}'")

set(_opencv_cmake_config
    "${OPENCV_ANDROID}/sdk/native/jni/abi-${ANDROID_ABI}/OpenCVConfig.cmake")

# Older OpenCV Android SDK (4.0~4.2) ships a single OpenCVConfig.cmake
# at sdk/native/jni/; it is selected by ANDROID_ABI automatically.
if(NOT EXISTS "${_opencv_cmake_config}")
  set(_opencv_cmake_config
      "${OPENCV_ANDROID}/sdk/native/jni/OpenCVConfig.cmake")
endif()

if(EXISTS "${_opencv_cmake_config}")
  message(STATUS "OpenCV CMake config: ${_opencv_cmake_config}")
  # OpenCV_DIR must point to the *directory* containing OpenCVConfig.cmake,
  # not to the file itself.
  get_filename_component(OpenCV_DIR "${_opencv_cmake_config}" DIRECTORY)
  find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs calib3d)
  set(OPENCV_LIBS ${OpenCV_LIBS})
else()
  # Last-ditch fallback for the standalone Linux test driver: rely on
  # a system OpenCV (apt install libopencv-dev) when no Android SDK
  # is provided.
  message(WARNING
    "OpenCV Android SDK not found at ${OPENCV_ANDROID}; falling back to "
    "system find_package(OpenCV). The build will fail on Android.")
  find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs calib3d)
  set(OPENCV_LIBS ${OpenCV_LIBS})
endif()

# ---- NCNN ----------------------------------------------------------------
# NCNN source tree. CI builds it via `scripts/build_ncnn_android.sh`,
# and produces a `ncnn/build-android/install` prefix that is exposed
# via $NCNN_ANDROID.

if(DEFINED ENV{NCNN_ANDROID})
  set(NCNN_ANDROID $ENV{NCNN_ANDROID})
endif()

if(EXISTS "${NCNN_ANDROID}/include/ncnn/net.h")
  include_directories(${NCNN_ANDROID}/include)
  set(NCNN_LIB "${NCNN_ANDROID}/lib/${ANDROID_ABI}/libncnn.a")
  if(NOT EXISTS "${NCNN_LIB}")
    set(NCNN_LIB "${NCNN_ANDROID}/lib/libncnn.a")
  endif()
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/ncnn/include/ncnn/net.h")
  # Bundled ncnn (when checked into the repo for self-contained CI).
  include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/ncnn/include)
  set(NCNN_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/ncnn/lib/${ANDROID_ABI}/libncnn.a")
else()
  find_package(ncnn QUIET)
  if(ncnn_FOUND)
    set(NCNN_LIB ncnn)
  else()
    message(FATAL_ERROR
      "ncnn not found. Set NCNN_ANDROID=/path/to/ncnn/install or vendor it into src/ncnn/3rdparty/ncnn/.")
  endif()
endif()

# ---- Sources -------------------------------------------------------------
set(PDR_NCNN_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/platedetector_yolov7_ncnn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/platerecognition_ncnn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/plate_algorithm_ncnn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/jni/jni_bridge.cpp)

# Common headers (DataTypes_Plate.h, etc.) live two levels up.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../..)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---- Target --------------------------------------------------------------
add_library(hyperlpr3 SHARED ${PDR_NCNN_SRCS})

target_compile_options(hyperlpr3 PRIVATE
    -O2 -fvisibility=hidden -fno-rtti -fno-exceptions -Wall
    -Wno-unused-parameter -Wno-deprecated-declarations)

# armv7 needs explicit float-abi and CPU tune; NDK r26b defaults to
# -mfloat-abi=softfp which is correct for armeabi-v7a + NEON. We pin
# it explicitly so a future NDK change can't silently break armv7.
if(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(hyperlpr3 PRIVATE
        -mfloat-abi=softfp -mfpu=neon -mthumb
        -fno-integrated-as)
endif()

target_link_libraries(hyperlpr3
    ${NCNN_LIB}
    ${OPENCV_LIBS}
    log
    android)

# On Android, strip release symbols.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set_target_properties(hyperlpr3 PROPERTIES LINK_FLAGS_RELEASE "-s")
endif()
