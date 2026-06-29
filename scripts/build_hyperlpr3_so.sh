#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Standalone build of `libhyperlpr3.so` for a single Android ABI using
# the NDK + the artefacts produced by `build_ncnn_android.sh`.
#
# Usage:
#   scripts/build_hyperlpr3_so.sh <ndk-dir> <ncnn-install> <opencv-android> <abi> <out-dir>
#
# This script is invoked by the GitHub Actions workflow
# `.github/workflows/build-so.yml`. It can also be run locally for
# debugging.

set -euo pipefail

NDK_DIR="${1:-${ANDROID_NDK_HOME:-}}"
NCNN_INSTALL="${2:-./build/ncnn-install}"
OPENCV_ANDROID="${3:-./build/opencv-android}"
TARGET_ABI="${4:-arm64-v8a}"
OUT_DIR="${5:-./build/so}"

if [[ -z "${NDK_DIR}" || ! -d "${NDK_DIR}" ]]; then
    echo "ERROR: NDK dir not provided." >&2
    exit 1
fi

case "$(uname -s)" in
    Linux*)  HOST_TAG="linux-x86_64";;
    Darwin*) HOST_TAG="darwin-x86_64";;
    MINGW*|MSYS*|CYGWIN*) HOST_TAG="windows-x86_64";;
    *) echo "Unsupported host: $(uname -s)"; exit 3;;
esac

TOOLCHAIN="${NDK_DIR}/toolchains/llvm/prebuilt/${HOST_TAG}"
API_LEVEL=21

mkdir -p "${OUT_DIR}/${TARGET_ABI}"
BUILD_DIR="./build/hyperlpr3-${TARGET_ABI}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

export NCNN_ANDROID="${NCNN_INSTALL}"
export OPENCV_ANDROID="${OPENCV_ANDROID}"
export ANDROID_NDK_HOME="${NDK_DIR}"
export PATH="${TOOLCHAIN}/bin:${PATH}"

cmake "../../PlateDetectionRecognition/src/ncnn" \
    -DCMAKE_TOOLCHAIN_FILE="${NDK_DIR}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="${TARGET_ABI}" \
    -DANDROID_PLATFORM="android-${API_LEVEL}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DANDROID_STL=c++_static

cmake --build . --parallel 2

# Copy the resulting .so into the requested output directory.
find . -name "libhyperlpr3.so" -exec cp -v {} "${OUT_DIR}/${TARGET_ABI}/" \;

echo "Built: ${OUT_DIR}/${TARGET_ABI}/libhyperlpr3.so"
ls -la "${OUT_DIR}/${TARGET_ABI}"
