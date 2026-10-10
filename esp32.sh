#!/usr/bin/env bash
#
# ESP32-WROOM 固件编译脚本（默认增量编译）
# 工程目录：esp32-wroom/（ESP-IDF v5.1，目标 esp32，产物 wireless_tx_2354.bin）
#
# 用法:
#   ./esp32.sh            增量编译（复用 esp32-wroom/build-esp/ 目录）
#   ./esp32.sh -c         全量编译（删除 build-esp/ 后重新配置并编译）
#
# 环境变量:
#   IDF_PATH              ESP-IDF 路径（默认 ~/esp/esp-idf）
#   ESP_PROXY             依赖拉取代理（默认 http://192.168.8.200:10808，置空可禁用）
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ESP_DIR="${PROJECT_DIR}/esp32-wroom"
BUILD_DIR="${ESP_DIR}/build-esp"
IDF_PATH="${IDF_PATH:-${HOME}/esp/esp-idf}"
ESP_PROXY="${ESP_PROXY-http://192.168.8.200:10808}"

CLEAN=0
if [ "${1:-}" = "-c" ] || [ "${1:-}" = "--clean" ]; then
    CLEAN=1
fi

if [ ! -f "${IDF_PATH}/export.sh" ]; then
    echo "错误：未找到 ESP-IDF（${IDF_PATH}/export.sh），请用 IDF_PATH 指定" >&2
    exit 1
fi

if [ "${CLEAN}" = "1" ]; then
    echo "==> 全量编译：删除 ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

echo "==> 加载 ESP-IDF 环境（${IDF_PATH}）"
# shellcheck disable=SC1091
. "${IDF_PATH}/export.sh" > /dev/null

# CMake 初始化的 git 子模块（如 esp_wifi/lib）不继承仓库 local 代理，
# 会回落到已失效的全局 socks5://192.168.8.242:10808，这里强制覆盖。
if [ -n "${ESP_PROXY}" ]; then
    export GIT_CONFIG_COUNT=1
    export GIT_CONFIG_KEY_0=http.proxy
    export GIT_CONFIG_VALUE_0="${ESP_PROXY}"
fi

# 目标芯片已固化在 esp32-wroom/sdkconfig（CONFIG_IDF_TARGET="esp32"），
# 全新 build-esp/ 直接 build 即可自动配置，无需 set-target（避免重写 sdkconfig）。
echo "==> 编译（build 目录：${BUILD_DIR}）"
idf.py -C "${ESP_DIR}" -B "${BUILD_DIR}" build

BIN_FILE="${BUILD_DIR}/wireless_tx_2354.bin"

# 将编译后的固件复制到 images 目录覆盖同名文件（该目录另含 CMake 收集的
# bootloader.bin / partition-table.bin / app.bin 三分体镜像）
IMAGES_DIR="${BUILD_DIR}/images"
mkdir -p "${IMAGES_DIR}"
cp -f "${BIN_FILE}" "${IMAGES_DIR}/"
echo "==> 编译完成，固件已复制到 ${IMAGES_DIR}/"
ls -1 "${IMAGES_DIR}"
