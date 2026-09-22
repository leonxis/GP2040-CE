#!/usr/bin/env bash
#
# HML2354 固件编译脚本（默认增量编译）
#
# 用法:
#   ./gp2354.sh            增量编译（复用 build/ 目录）
#   ./gp2354.sh -c         全量编译（删除 build/ 后重新配置并编译）
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
PICO_SDK_PATH="${PICO_SDK_PATH:-/home/leonxis/GP2040/pico-sdk}"

CLEAN=0
if [ "${1:-}" = "-c" ] || [ "${1:-}" = "--clean" ]; then
    CLEAN=1
fi

if [ "${CLEAN}" = "1" ]; then
    echo "==> 全量编译：删除 ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

# 首次使用或全量编译时配置 CMake
if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    echo "==> 配置 CMake (PICO_BOARD=HML2354, rp2350-arm-s, Release)"
    mkdir -p "${BUILD_DIR}"
    cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
        -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=Release \
        -DPICO_BOARD=HML2354 \
        -DPICO_PLATFORM=rp2350-arm-s \
        -DPICO_SDK_PATH="${PICO_SDK_PATH}"
fi

echo "==> 增量编译"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "==> 编译完成，固件输出："
ls -1 "${BUILD_DIR}"/GP2040-CE_*_HML2354.{uf2,bin}
