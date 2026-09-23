#ifndef _NRF24_LINK_H_
#define _NRF24_LINK_H_

#include "gpaddon.h"
#include "types.h"
#include "peripheral_spi.h"
#include "peripheralmanager.h"

#include "hardware/gpio.h"
#include "pico/time.h"

// 通过 NRF24LinkAddon 控制是否启用（由 GamepadOptions.nrf24LinkEnabled 驱动）。
// BoardConfig 提供 SPI0 引脚（CE/CS/SCK/TX/RX），插件通过 SPI0 直驱 nRF24L01+。
#ifndef NRF24_LINK_ENABLED
#define NRF24_LINK_ENABLED 1
#endif

#ifndef DEFAULT_NRF24_LINK_ENABLED
#define DEFAULT_NRF24_LINK_ENABLED 0
#endif

#define NRF24_LINK_ADDON_NAME "NRF24 Link"

// HML2354 固定接线：SPI0（SCK/TX/RX 来自 BoardConfig），CSN/CE 由 BoardConfig 提供
static constexpr uint8_t NRF24_HW_SPI_BLOCK = 0;
static constexpr int8_t  NRF24_HW_CS_PIN    = SPI0_PIN_CS;   // nRF24 CSN
static constexpr int8_t  NRF24_HW_CE_PIN    = SPI0_PIN_CE;   // nRF24 CE

// SPI 速率：4MHz（与 ESP32 端 nrf24.h 一致）
#define NRF24_SPI_HZ          4000000u

// nRF24 RF 配置（与 ESP32 端完全一致以确保互通）
#define NRF24_PAYLOAD         15
#define NRF24_CHANNEL         100   // 2.500 GHz

// nRF24 寄存器地址
#define NRF24_REG_CONFIG      0x00
#define NRF24_REG_EN_AA       0x01
#define NRF24_REG_EN_RXADDR   0x02
#define NRF24_REG_SETUP_AW    0x03
#define NRF24_REG_SETUP_RETR 0x04
#define NRF24_REG_RF_CH       0x05
#define NRF24_REG_RF_SETUP    0x06
#define NRF24_REG_STATUS      0x07
#define NRF24_REG_RX_ADDR_P0  0x0A
#define NRF24_REG_TX_ADDR    0x10
#define NRF24_REG_RX_PW_P0   0x11

// nRF24 命令
#define NRF24_CMD_R_RX_PAYLOAD 0x61
#define NRF24_CMD_W_TX_PAYLOAD 0xA0
#define NRF24_CMD_FLUSH_TX     0xE1

// STATUS 位
#define NRF24_STATUS_TX_DS     0x20
#define NRF24_STATUS_MAX_RT    0x10
#define NRF24_STATUS_RX_DR    0x40

// INPUT 帧恒流发送间隔（微秒）：芯片空闲且距上次发送 ≥1ms 即无条件发当前
// 状态快照，与 ESP32 端 radioTask 定频（2ms）同语义。接收器带丢包超时回中
// 保护（摇杆静止时 50ms 心跳间隙会周期性触发回中闪现），恒流保证包间隙
// 最多为在飞事务时长（最坏 ~2ms），远低于回中阈值。
#define NRF24_SEND_INTERVAL_US      1000

// 异步 TX：CE 高电平保持（datasheet 要求 >10us 触发单包发射）
#define NRF24_CE_PULSE_US           15
// 异步 TX 软件超时：SETUP_RETR(0x13：250us/3次重发) 下硬件最长事务 ~2ms，
// 2.5ms 无 TX_DS/MAX_RT 判芯片异常，flush 后恢复
#define NRF24_TX_POLL_TIMEOUT_US    2500

// 接收端在线去抖：连续 N 次 ACK 判在线；ACK 静默超过超时（≈20 个心跳）判离线。
// 状态写 Storage::setNrf24LinkUp()，供 Core1 环境光未配对红闪消费。
#define NRF24_LINK_UP_STREAK        2
#define NRF24_LINK_DOWN_TIMEOUT_US  1000000

// 异步 TX 设计：postprocess 不等待 RF ACK。提交帧只做 W_TX_PAYLOAD（~35us
// SPI）+ CE 脉冲（15us）即返回；后续帧 postprocess 每次只读一次 STATUS（~5us）
// 判定 TX_DS/MAX_RT/超时。在飞期间不写第二个 W_TX_PAYLOAD（txPending 门控，
// nRF24 TX FIFO 恒为 ≤1 包，重发只拉长单事务到最坏 ~2ms），芯片空闲即按恒流
// 间隔发最新快照（数据不缓存，始终从 Storage 读取）。postprocess 对主循环
// 占用稳定 ≤ ~70us，与接收端是否在线无关。

class NRF24LinkAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void preprocess() {}
    virtual void process();
    virtual void postprocess(bool sent);
    virtual std::string name() { return NRF24_LINK_ADDON_NAME; }
    virtual void reinit();

private:
    // SPI 寄存器读写原语（通过 CSN 选片）
    void writeReg(uint8_t reg, uint8_t val);
    void writeRegBuf(uint8_t reg, const uint8_t *data, uint8_t len);
    uint8_t readReg(uint8_t reg);
    void flushTx();
    // 拉高 CE 指定微秒数（TX 触发 / RX 使能）
    void cePulse(uint32_t us);
    // 将 payload 写入 TX FIFO 并脉冲 CE 触发发射（不等待 ACK），置 txPending
    void submitPacket(const uint8_t *data);
    // 单次 STATUS 读取判定发射结果：
    //   1=ACK(TX_DS)，0=失败(MAX_RT/软件超时，已 flush)，-1=仍在进行
    int8_t pollTxComplete();
    // 组装 15 字节 payload 并提交（ACK 结果由后续帧 pollTxComplete 获得）
    void submitInputFrame(uint16_t buttons, uint8_t dpad,
                          uint16_t lx, uint16_t ly,
                          uint16_t rx, uint16_t ry,
                          uint8_t lt, uint8_t rt, uint8_t inputMode);

    PeripheralSPI* spi_ = nullptr;
    SPIBaudrateProfile spiProfile_ = {};
    int8_t csPin_ = -1;
    int8_t cePin_ = -1;
    bool initialized = false;

    // 恒流节流：上次发送时刻
    uint32_t lastSentUs = 0;

    // payload[1] 递增序号：与 ESP32 端 radioSeq 行为一致，供接收端丢包统计
    uint8_t  txSeq = 0;

    // 接收端在线去抖状态（ACK 历史），跨核通过 Storage nrf24LinkUp 发布
    bool     linkUp = false;
    uint8_t  linkAckStreak = 0;
    uint32_t lastAckUs = 0;

    // 异步 TX 状态：txPending=有在飞事务（nRF24 TX FIFO 仅 1 包）；
    // txStartUs=本次事务启动时刻（软件超时用）
    bool     txPending = false;
    uint32_t txStartUs = 0;
};

#endif
