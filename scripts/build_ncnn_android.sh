#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build NCNN for Android from source. Produces a static lib (`libncnn.a`)
# for arm64-v8a and armeabi-v7a under `$NCNN_INSTALL/lib/<abi>/` plus
# the `onnx2ncnn` and `ncnnoptimize` tools in `$NCNN_INSTALL/bin/`.
#
# Usage:
#   scripts/build_ncnn_android.sh <src-dir> <install-dir> <ndk-dir> <abi>
#
# Examples:
#   scripts/build_ncnn_android.sh ./ncnn ./build/ncnn-install \
#       $ANDROID_NDK_HOME arm64-v8a
#   scripts/build_ncnn_android.sh ./ncnn ./build/ncnn-install \
#       $ANDROID_NDK_HOME armeabi-v7a

set -euo pipefail

NCNN_SRC="${1:-./ncnn}"
INSTALL_DIR="${2:-./build/ncnn-install}"
NDK_DIR="${3:-${ANDROID_NDK_HOME:-}}"
TARGET_ABI="${4:-arm64-v8a}"

# Resolve absolute paths up-front; the script `cd`s later and
# relative inputs would then resolve under the wrong directory.
NCNN_SRC="$(cd "$(dirname "${NCNN_SRC}")" && pwd)/$(basename "${NCNN_SRC}")"
mkdir -p "$(dirname "${INSTALL_DIR}")" 2>/dev/null || true
INSTALL_DIR="$(cd "$(dirname "${INSTALL_DIR}")" && pwd)/$(basename "${INSTALL_DIR}")"

if [[ -z "${NDK_DIR}" || ! -d "${NDK_DIR}" ]]; then
    echo "ERROR: Android NDK directory not provided. Set ANDROID_NDK_HOME or pass as 3rd arg." >&2
    exit 1
fi

case "${TARGET_ABI}" in
    arm64-v8a)      ANDROID_ABI="arm64-v8a";   NCNN_ABI="arm64";;
    armeabi-v7a)    ANDROID_ABI="armeabi-v7a"; NCNN_ABI="armv7";;
    x86_64)         ANDROID_ABI="x86_64";      NCNN_ABI="x86_64";;
    x86)            ANDROID_ABI="x86";         NCNN_ABI="x86";;
    *) echo "Unsupported ABI: ${TARGET_ABI}"; exit 2;;
esac

# Use the host toolchain shipped with the NDK.
case "$(uname -s)" in
    Linux*)  HOST_TAG="linux-x86_64";;
    Darwin*) HOST_TAG="darwin-x86_64";;
    MINGW*|MSYS*|CYGWIN*) HOST_TAG="windows-x86_64";;
    *) echo "Unsupported host: $(uname -s)"; exit 3;;
esac

TOOLCHAIN="${NDK_DIR}/toolchains/llvm/prebuilt/${HOST_TAG}"

# Pull NCNN source if missing. Tag pinned for reproducibility; bump as
# needed.
NCNN_VERSION="${NCNN_VERSION:-20240820}"
if [[ ! -d "${NCNN_SRC}" ]]; then
    echo "Cloning NCNN ${NCNN_VERSION}..."
    git clone --depth 1 --branch "${NCNN_VERSION}" \
        https://github.com/Tencent/ncnn.git "${NCNN_SRC}"
fi

mkdir -p "${INSTALL_DIR}"
NCNN_BUILD_DIR="${NCNN_SRC}/build-android-${ANDROID_ABI}"
mkdir -p "${NCNN_BUILD_DIR}"

cd "${NCNN_BUILD_DIR}"

cmake "${NCNN_SRC}" \
    -DCMAKE_TOOLCHAIN_FILE="${NDK_DIR}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="${ANDROID_ABI}" \
    -DANDROID_PLATFORM="android-21" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNCNN_BUILD_TOOLS=ON \
    -DNCNN_BUILD_EXAMPLES=OFF \
    -DNCNN_BUILD_BENCHMARK=OFF \
    -DNCNN_BUILD_TESTS=OFF \
    -DNCNN_VULKAN=OFF \
    -DNCNN_PYTHON=OFF \
    -DNCNN_SHARED_LIB=OFF \
    -DNCNN_OPENMP=ON \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON

cmake --build . --parallel
cmake --install .

# Place the .a in a predictable per-ABI subdir for the consumer
# CMakeLists.
mkdir -p "${INSTALL_DIR}/lib/${ANDROID_ABI}"
if [[ -f "${INSTALL_DIR}/lib/libncnn.a" ]]; then
    cp -f "${INSTALL_DIR}/lib/libncnn.a" "${INSTALL_DIR}/lib/${ANDROID_ABI}/libncnn.a"
fi

echo "NCNN installed under ${INSTALL_DIR}"
ls -la "${INSTALL_DIR}/lib" 2>/dev/null || true
