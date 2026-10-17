#pragma once

// ===========================================================================
// ESP32-S3 板级配置：USB Host + nRF24 SPI 引脚
// 引脚分配来自项目历史 ESP32-S3 工程（commit 8d76def0^）
// ===========================================================================

// --- USB OTG（ESP32-S3 固定引脚，GPIO19=D-, GPIO20=D+）---
#define USB_DP_GPIO         20
#define USB_DM_GPIO         19

// --- nRF24L01+（HSPI 总线）---
#define NRF_SPI_HOST        SPI2_HOST
#define NRF_SPI_PIN_MOSI    11
#define NRF_SPI_PIN_MISO    13
#define NRF_SPI_PIN_SCK     12
#define NRF_SPI_PIN_CSN     10
#define NRF_PIN_CE          9

// --- 状态灯 WS2812（可选）---
#define LED_PIN             21

// --- nRF24 链路参数（与项目 nrf24_link.h / wireless-rx 接收端对齐）---
#define NRF_PAYLOAD         15
#define NRF_CHANNEL         84      // 2.484 GHz
#define NRF_ADDR_B0         0xE7
#define NRF_ADDR_B1         0xE7
#define NRF_ADDR_B2         0xE7
#define NRF_ADDR_B3         0xE7
#define NRF_ADDR_B4         0xE7

// SPI 速率 8MHz / MODE0 / MSB（与 nrf24_link.h NRF24_SPI_HZ 一致）
#define NRF_SPI_HZ          8000000

// 发射时序（与 nrf24_link.cpp 对齐）
#define NRF_CE_PULSE_US     15
#define NRF_TX_TIMEOUT_US   2500
#define NRF_SEND_INTERVAL_US 1000
#define NRF_FAIL_REINIT_THRESHOLD 500
