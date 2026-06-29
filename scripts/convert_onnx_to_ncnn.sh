#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Convert plate detection / recognition ONNX models to NCNN format.
# Idempotent: re-runs the conversion only if the source ONNX is newer
# than the destination .param/.bin pair, or if forced.
#
# Usage:
#   scripts/convert_onnx_to_ncnn.sh <onnx-tools-bin-dir> <models-src-dir> <models-out-dir> [force]
#
# The NCNN distribution ships `onnx2ncnn` next to the libraries. The
# first positional argument points at the directory containing that
# binary. In CI we build NCNN from source with `-DNCNN_BUILD_TOOLS=ON`
# so the tool is always available.

set -euo pipefail

ONNX2NCNN_BIN="${1:-./ncnn/build/tools/onnx2ncnn}"
MODELS_SRC="${2:-./PlateDetectionRecognition/test}"
MODELS_OUT="${3:-./hyperlpr3-android-sdk-master/hyperlpr3/src/main/assets/plate_ncnn}"
FORCE="${4:-0}"

# Accept either the path to the onnx2ncnn binary, or a directory that
# contains it. Most CI setups build NCNN to <install>/bin/ and pass the
# directory, so resolve it automatically.
if [[ -d "${ONNX2NCNN_BIN}" ]]; then
    if [[ -x "${ONNX2NCNN_BIN}/onnx2ncnn" ]]; then
        ONNX2NCNN_BIN="${ONNX2NCNN_BIN}/onnx2ncnn"
    fi
fi

if [[ ! -x "${ONNX2NCNN_BIN}" ]]; then
    echo "ERROR: onnx2ncnn binary not found at: ${ONNX2NCNN_BIN}" >&2
    echo "Build ncnn with -DNCNN_BUILD_TOOLS=ON and pass the path or" \
         "containing directory here." >&2
    exit 1
fi

mkdir -p "${MODELS_OUT}"

convert_one() {
    local onnx="$1"
    local name
    name="$(basename "${onnx}" .onnx)"
    local param="${MODELS_OUT}/${name}.param"
    local bin="${MODELS_OUT}/${name}.bin"

    if [[ "${FORCE}" != "1" && -f "${param}" && -f "${bin}" \
          && "${param}" -nt "${onnx}" && "${bin}" -nt "${onnx}" ]]; then
        echo "SKIP ${name}: up-to-date"
        return
    fi

    echo "CONVERT ${name} (${onnx})"
    "${ONNX2NCNN_BIN}" "${onnx}" "${param}" "${bin}"
    # Reduce fp32 weights to fp16 to shrink the .bin for mobile.
    # Prefer ncnnoptimize from the same directory as onnx2ncnn.
    local optimize
    optimize="$(dirname "${ONNX2NCNN_BIN}")/ncnnoptimize"
    if [[ -x "${optimize}" ]]; then
        "${optimize}" "${param}" "${bin}" "${param}.opt" "${bin}.opt" 65536
        mv -f "${param}.opt" "${param}"
        mv -f "${bin}.opt"   "${bin}"
    fi
}

# Plate detector (yolov7). We keep both yolov5 and yolov7 sources; only
# yolov7 is used by default but the assets folder keeps the other one
# available.
if [[ -f "${MODELS_SRC}/yolov7plate.onnx" ]]; then
    convert_one "${MODELS_SRC}/yolov7plate.onnx"
else
    echo "WARN: yolov7plate.onnx not found in ${MODELS_SRC}" >&2
fi

# Plate recognition (text + color).
if [[ -f "${MODELS_SRC}/plate_recognition_color.onnx" ]]; then
    convert_one "${MODELS_SRC}/plate_recognition_color.onnx"
else
    echo "WARN: plate_recognition_color.onnx not found in ${MODELS_SRC}" >&2
fi

echo "Done. Outputs in ${MODELS_OUT}:"
ls -la "${MODELS_OUT}"
