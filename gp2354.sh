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

# 强制板型为 HML2354：CMakeLists 中环境变量 GP2040_BOARDCONFIG 优先级高于 -D，
# 必须覆盖 shell 导出（如 ~/.bashrc 里的 HML），否则 include 路径会指向不存在的 configs/HML
export GP2040_BOARDCONFIG=HML2354

CLEAN=0
if [ "${1:-}" = "-c" ] || [ "${1:-}" = "--clean" ]; then
    CLEAN=1
fi

if [ "${CLEAN}" = "1" ]; then
    echo "==> 全量编译：删除 ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

# 以下情况需要（重新）配置 CMake：
# 1) 首次使用或全量编译（无 CMakeCache）
# 2) 已生成的编译参数里板型 include 路径不是 HML2354（曾被 shell 环境变量污染）
FLAGS_MAKE="${BUILD_DIR}/CMakeFiles/GP2040-CE.dir/flags.make"
NEED_CONFIGURE=0
if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    NEED_CONFIGURE=1
elif [ -f "${FLAGS_MAKE}" ] && ! grep -q "configs/HML2354" "${FLAGS_MAKE}"; then
    echo "==> 检测到构建目录板型配置被污染，重新配置 CMake（不影响增量产物）"
    NEED_CONFIGURE=1
fi

if [ "${NEED_CONFIGURE}" = "1" ]; then
    echo "==> 配置 CMake (PICO_BOARD=HML2354, rp2350-arm-s, Release)"
    mkdir -p "${BUILD_DIR}"
    cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
        -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=Release \
        -DGP2040_BOARDCONFIG=HML2354 \
        -DPICO_BOARD=HML2354 \
        -DPICO_PLATFORM=rp2350-arm-s \
        -DPICO_SDK_PATH="${PICO_SDK_PATH}"
fi

echo "==> 增量编译"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "==> 编译完成，固件输出："
ls -1 "${BUILD_DIR}"/GP2040-CE_*_HML2354.{uf2,bin}
