# SPDX-License-Identifier: MIT
# Standalone NDK build for `libhyperlpr3.so` that replaces the legacy
# tensorrt/CUDA-based implementation. Targets the same Java JNI surface
# (`com.hyperai.hyperlpr3.core.HyperLPRCore`) and is consumed by
# hyperlpr3-android-sdk.
#
# This file is meant to be invoked from an Android Gradle externalNativeBuild
# block, but it also works as a plain CMake project for building .so
# artefacts from CI.

cmake_minimum_required(VERSION 3.18.1)

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
# $OPENCV_ANDROID path.

if(DEFINED ENV{OPENCV_ANDROID})
  set(OPENCV_ANDROID $ENV{OPENCV_ANDROID})
endif()

if(EXISTS "${OPENCV_ANDROID}/sdk/native/jni/include")
  include_directories(${OPENCV_ANDROID}/sdk/native/jni/include)
  set(OPENCV_LIBS_DIR "${OPENCV_ANDROID}/sdk/native/jni/${ANDROID_ABI}")
  set(OPENCV_LIBS
      ${OPENCV_LIBS_DIR}/libopencv_core.a
      ${OPENCV_LIBS_DIR}/libopencv_imgproc.a
      ${OPENCV_LIBS_DIR}/libopencv_imgcodecs.a
      ${OPENCV_LIBS_DIR}/libopencv_calib3d.a)
else()
  # Fall back to a system OpenCV (used by the standalone test driver on
  # Linux). The Android build always sets OPENCV_ANDROID above.
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
    -O2 -fvisibility=hidden -fno-rtti -fno-exceptions -Wall)

target_link_libraries(hyperlpr3
    ${NCNN_LIB}
    ${OPENCV_LIBS}
    log
    android)

# On Android, strip release symbols.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set_target_properties(hyperlpr3 PROPERTIES LINK_FLAGS_RELEASE "-s")
endif()
