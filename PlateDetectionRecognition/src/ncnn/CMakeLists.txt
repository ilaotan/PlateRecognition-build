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
# $OPENCV_ANDROID path. We support the multiple layouts that the
# upstream OpenCV Android SDK ships, depending on version:
#   4.10+    -> sdk/native/staticlibs/<abi>/ + sdk/native/libs/<abi>/
#   4.5~4.8  -> sdk/native/jni/3rdparty/libs/<abi>/ + sdk/native/jni/<abi>/
#   4.0~4.2  -> sdk/native/libs/<abi>/ (shared only)

if(DEFINED ENV{OPENCV_ANDROID})
  set(OPENCV_ANDROID $ENV{OPENCV_ANDROID})
endif()

set(_opencv_h_dirs
    "${OPENCV_ANDROID}/sdk/native/jni/include"
    "${OPENCV_ANDROID}/sdk/native/staticlibs/include")  # future layout
foreach(d IN LISTS _opencv_h_dirs)
  if(EXISTS "${d}/opencv2/core.hpp")
    include_directories(${d})
    set(_opencv_inc "${d}")
    break()
  endif()
endforeach()

if(NOT _opencv_inc)
  # Fall back to a system OpenCV (used by the standalone test driver on
  # Linux). The Android build always sets OPENCV_ANDROID above.
  find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs calib3d)
  set(OPENCV_LIBS ${OpenCV_LIBS})
else()
  set(OPENCV_LIBS "")
  set(_opencv_candidate_dirs
      "${OPENCV_ANDROID}/sdk/native/jni/3rdparty/libs/${ANDROID_ABI}"
      "${OPENCV_ANDROID}/sdk/native/staticlibs/${ANDROID_ABI}")
  foreach(d IN LISTS _opencv_candidate_dirs)
    if(EXISTS "${d}/libopencv_core.a")
      foreach(m core imgproc imgcodecs calib3d)
        list(APPEND OPENCV_LIBS "${d}/libopencv_${m}.a")
      endforeach()
      break()
    endif()
  endforeach()

  if(NOT OPENCV_LIBS AND EXISTS "${OPENCV_ANDROID}/sdk/native/libs/${ANDROID_ABI}")
    # Fall back to the shared .so from a packaged OpenCV Android SDK.
    foreach(m core imgproc imgcodecs calib3d)
      list(APPEND OPENCV_LIBS
           "${OPENCV_ANDROID}/sdk/native/libs/${ANDROID_ABI}/libopencv_${m}.so")
    endforeach()
  endif()

  if(NOT OPENCV_LIBS)
    message(FATAL_ERROR
      "OpenCV libs not found for ${ANDROID_ABI} under ${OPENCV_ANDROID}. "
      "Tried sdk/native/jni/3rdparty/libs, sdk/native/staticlibs and "
      "sdk/native/libs.")
  endif()
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
