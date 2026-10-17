#pragma once
#include <stdint.h>
#include <stdbool.h>

// ===========================================================================
// nRF24L01+ 驱动（与 GP2040-CE nrf24_link.cpp 配置对齐）
// ===========================================================================

// nRF24 寄存器地址
#define NRF_REG_CONFIG      0x00
#define NRF_REG_EN_AA       0x01
#define NRF_REG_EN_RXADDR   0x02
#define NRF_REG_SETUP_AW    0x03
#define NRF_REG_SETUP_RETR  0x04
#define NRF_REG_RF_CH       0x05
#define NRF_REG_RF_SETUP    0x06
#define NRF_REG_STATUS      0x07
#define NRF_REG_RX_ADDR_P0  0x0A
#define NRF_REG_TX_ADDR     0x10
#define NRF_REG_RX_PW_P0    0x11

// nRF24 命令
#define NRF_CMD_R_RX_PAYLOAD    0x61
#define NRF_CMD_W_TX_PAYLOAD    0xA0
#define NRF_CMD_FLUSH_TX        0xE1
#define NRF_CMD_FLUSH_RX        0xE2

// STATUS 位
#define NRF_STATUS_TX_DS    0x20
#define NRF_STATUS_MAX_RT   0x10
#define NRF_STATUS_RX_DR    0x40

// 初始化 nRF24（SPI + 寄存器配置），与 nrf24_link.cpp::setup() 对齐
void nrf24_init(void);

// 提交 15 字节 payload 并触发发射（异步，不等待 ACK）
void nrf24_submitPacket(const uint8_t *data);

// 轮询发射结果：1=ACK(TX_DS), 0=失败(MAX_RT/超时已flush), -1=仍在进行
int8_t nrf24_pollTxComplete(void);

// 组装 15 字节帧并提交（内部维护 txSeq）
void nrf24_sendFrame(uint8_t inputMode, uint16_t buttons, uint8_t dpad,
                     uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry,
                     uint8_t lt, uint8_t rt);

// 重初始化（连续失败看门狗用）
void nrf24_reinit(void);
