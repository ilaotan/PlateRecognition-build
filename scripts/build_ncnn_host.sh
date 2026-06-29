#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build NCNN host tools (`onnx2ncnn`, `ncnnoptimize`) for the build
# machine. These are used by `convert_onnx_to_ncnn.sh` to translate
# ONNX models into NCNN's .param/.bin format. They cannot be produced
# reliably by the Android NDK toolchain (they would be cross-compiled
# into ARM binaries that cannot run on the CI host), so we build them
# separately with the host compiler.
#
# Usage:
#   scripts/build_ncnn_host.sh <src-dir> <install-dir>
#
# Examples:
#   scripts/build_ncnn_host.sh ./ncnn ./build/ncnn-host-install

set -euo pipefail

NCNN_SRC="${1:-./ncnn}"
INSTALL_DIR="${2:-./build/ncnn-host-install}"

NCNN_SRC="$(cd "$(dirname "${NCNN_SRC}")" && pwd)/$(basename "${NCNN_SRC}")"
mkdir -p "$(dirname "${INSTALL_DIR}")" 2>/dev/null || true
INSTALL_DIR="$(cd "$(dirname "${INSTALL_DIR}")" && pwd)/$(basename "${INSTALL_DIR}")"

NCNN_VERSION="${NCNN_VERSION:-20240820}"
if [[ ! -d "${NCNN_SRC}" ]]; then
    echo "Cloning NCNN ${NCNN_VERSION}..."
    git clone --depth 1 --branch "${NCNN_VERSION}" \
        https://github.com/Tencent/ncnn.git "${NCNN_SRC}"
fi

BUILD_DIR="${NCNN_SRC}/build-host"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${NCNN_SRC}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNCNN_BUILD_TOOLS=ON \
    -DNCNN_BUILD_EXAMPLES=OFF \
    -DNCNN_BUILD_BENCHMARK=OFF \
    -DNCNN_BUILD_TESTS=OFF \
    -DNCNN_VULKAN=OFF \
    -DNCNN_PYTHON=OFF \
    -DNCNN_SHARED_LIB=OFF \
    -DNCNN_OPENMP=OFF \
    -DNCNN_AVX2=OFF \
    -DNCNN_AVX512=OFF \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"

cmake --build . --parallel 2
cmake --install .

# Verify the tools actually landed in <install>/bin; otherwise fail
# fast with a clear message (the workflow would otherwise be very
# confusing if a downstream step tries to run a missing binary).
if [[ ! -x "${INSTALL_DIR}/bin/onnx2ncnn" ]]; then
    echo "ERROR: onnx2ncnn not produced at ${INSTALL_DIR}/bin/onnx2ncnn" >&2
    exit 2
fi

echo "NCNN host tools installed under ${INSTALL_DIR}"
ls -la "${INSTALL_DIR}/bin"
