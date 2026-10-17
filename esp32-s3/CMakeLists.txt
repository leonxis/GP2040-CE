# ESP32-S3 USB Host -> nRF24 转发工程
# 枚举 USB 手柄（NS/DS/XINPUT），解析输入后通过 nRF24 转发给接收端
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_usb_nrf24)
