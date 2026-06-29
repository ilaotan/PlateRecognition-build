# SPDX-License-Identifier: MIT
# CMakeLists.txt invoked by Android Gradle's externalNativeBuild.
# Mirrors the standalone CMakeLists.txt in
#   PlateDetectionRecognition/src/ncnn/CMakeLists.txt
# but is self-contained for the Android library module and resolves
# NCNN + OpenCV from environment variables / vendored copies.
#
# Expected environment / cache variables:
#   NCNN_ROOT        - path to the NCNN Android install prefix
#                      (lib/<abi>/libncnn.a, include/ncnn/...)
#   OPENCV_ANDROID   - path to the OpenCV Android SDK
#                      (sdk/native/jni/...)

cmake_minimum_required(VERSION 3.22.1)
project(hyperlpr3 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Sources - keep the same as the standalone build to avoid drift.
set(PDR_NCNN_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../PlateDetectionRecognition/src/ncnn/platedetector_yolov7_ncnn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../PlateDetectionRecognition/src/ncnn/platerecognition_ncnn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../PlateDetectionRecognition/src/ncnn/plate_algorithm_ncnn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../PlateDetectionRecognition/src/ncnn/jni/jni_bridge.cpp)

set(PDR_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../PlateDetectionRecognition/src/ncnn
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../PlateDetectionRecognition/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../)

# ---- NCNN ----------------------------------------------------------------
if(NOT DEFINED NCNN_ROOT)
    if(DEFINED ENV{NCNN_ROOT})
        set(NCNN_ROOT $ENV{NCNN_ROOT})
    elseif(DEFINED ENV{NCNN_ANDROID})
        set(NCNN_ROOT $ENV{NCNN_ANDROID})
    else()
        message(FATAL_ERROR
            "Set NCNN_ROOT or NCNN_ANDROID to the NCNN Android install prefix.")
    endif()
endif()

if(NOT EXISTS "${NCNN_ROOT}/include/ncnn/net.h")
    message(FATAL_ERROR
        "ncnn headers not found under ${NCNN_ROOT}/include. "
        "Make sure you ran scripts/build_ncnn_android.sh.")
endif()

list(APPEND PDR_INCLUDE_DIRS "${NCNN_ROOT}/include")

# Pick the per-ABI .a. AGP sets ANDROID_ABI for us.
set(_ncnn_candidates
    "${NCNN_ROOT}/lib/${ANDROID_ABI}/libncnn.a"
    "${NCNN_ROOT}/libncnn.a"
    "${NCNN_ROOT}/${ANDROID_ABI}/libncnn.a")
set(NCNN_LIB "")
foreach(c IN LISTS _ncnn_candidates)
    if(EXISTS "${c}")
        set(NCNN_LIB "${c}")
        break()
    endif()
endforeach()
if(NCNN_LIB STREQUAL "")
    message(FATAL_ERROR
        "libncnn.a not found for ABI ${ANDROID_ABI} under ${NCNN_ROOT}.")
endif()

# ---- OpenCV --------------------------------------------------------------
if(NOT DEFINED OPENCV_ROOT)
    if(DEFINED ENV{OPENCV_ANDROID})
        set(OPENCV_ROOT $ENV{OPENCV_ANDROID})
    else()
        message(FATAL_ERROR "Set OPENCV_ROOT or OPENCV_ANDROID to the OpenCV Android SDK.")
    endif()
endif()

if(NOT EXISTS "${OPENCV_ROOT}/sdk/native/jni/include/opencv2/core.hpp")
    message(FATAL_ERROR
        "opencv2 headers not found under ${OPENCV_ROOT}/sdk/native/jni/include. "
        "Download OpenCV Android SDK 4.x and set OPENCV_ANDROID.")
endif()

list(APPEND PDR_INCLUDE_DIRS "${OPENCV_ROOT}/sdk/native/jni/include")

set(_opencv_libs_dir "${OPENCV_ROOT}/sdk/native/jni/3rdparty/libs/${ANDROID_ABI}")
set(OPENCV_LIBS)
if(EXISTS "${_opencv_libs_dir}")
    set(OPENCV_LIBS
        ${_opencv_libs_dir}/libopencv_core.a
        ${_opencv_libs_dir}/libopencv_imgproc.a
        ${_opencv_libs_dir}/libopencv_imgcodecs.a
        ${_opencv_libs_dir}/libopencv_calib3d.a)
    set(OPENCV_LIBS_DIR "${_opencv_libs_dir}")
elseif(EXISTS "${OPENCV_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}")
    set(OPENCV_LIBS
        ${OPENCV_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_core.a
        ${OPENCV_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_imgproc.a
        ${OPENCV_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_imgcodecs.a
        ${OPENCV_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_calib3d.a)
    set(OPENCV_LIBS_DIR "${OPENCV_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}")
elseif(EXISTS "${OPENCV_ROOT}/sdk/native/libs/${ANDROID_ABI}")
    # Shared libs fallback (.so). Need to be packaged into jniLibs.
    set(OPENCV_LIBS_DIR "${OPENCV_ROOT}/sdk/native/libs/${ANDROID_ABI}")
    foreach(m core imgproc imgcodecs calib3d)
        list(APPEND OPENCV_LIBS "${OPENCV_LIBS_DIR}/libopencv_${m}.so")
    endforeach()
else()
    message(FATAL_ERROR
        "OpenCV libs not found for ${ANDROID_ABI} under ${OPENCV_ROOT}. "
        "Expected one of sdk/native/jni/3rdparty/libs or sdk/native/staticlibs.")
endif()

# ---- Target --------------------------------------------------------------
add_library(hyperlpr3 SHARED ${PDR_NCNN_SRCS})

target_include_directories(hyperlpr3 PRIVATE ${PDR_INCLUDE_DIRS})

# Match the legacy `System.loadLibrary("hyperlpr3")` lookup in
# `com.hyperai.hyperlpr3.core.HyperLPRCore`.
set_target_properties(hyperlpr3 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/${ANDROID_ABI}")

target_compile_options(hyperlpr3 PRIVATE
    -O2 -fvisibility=hidden -fno-rtti -fno-exceptions -Wall
    -Wno-unused-parameter -Wno-deprecated-declarations)

# armv7 needs explicit float-abi / FPU / thumb flags; without these,
# NDK r26b may default to a configuration that is incompatible with
# the NCNN static lib built for armeabi-v7a + NEON.
if(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(hyperlpr3 PRIVATE
        -mfloat-abi=softfp -mfpu=neon -mthumb
        -fno-integrated-as)
endif()

# libncnn needs a static libz on some NDK versions; the NDK ships one.
find_library(log-lib log)
find_library(android-lib android)
find_library(z-lib z)

target_link_libraries(hyperlpr3
    ${NCNN_LIB}
    ${OPENCV_LIBS}
    ${log-lib}
    ${android-lib}
    ${z-lib})

# Strip in release.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(hyperlpr3 PROPERTIES LINK_FLAGS_RELEASE "-s")
endif()
