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

// INPUT 帧发送节流（微秒）：数字键变化即发；模拟量连续变化最小间隔 900us；
// 50ms 心跳保证空闲时也有保底同步。
#define NRF24_INPUT_ANALOG_MIN_US   900
#define NRF24_INPUT_HEARTBEAT_US    50000

// 接收端在线去抖：连续 N 次 ACK 判在线；ACK 静默超过超时（≈20 个心跳）判离线。
// 状态写 Storage::setNrf24LinkUp()，供 Core1 环境光未配对红闪消费。
#define NRF24_LINK_UP_STREAK        2
#define NRF24_LINK_DOWN_TIMEOUT_US  1000000

// DMA 扩展点：当前使用 PeripheralSPI 现有 FIFO 阻塞传输（spi_write_read_blocking），
// 已使用 8 字节深硬件 FIFO，16 字节负载 @4MHz 仅 32us SPI 时间。
// nRF24 主要瓶颈是 130us 空中 ACK 等待，与 SPI 无关；DMA 净收益约 17us/帧（1.7% CPU @1000Hz），
// 不抵配置复杂度。若未来扩展为多包连续发送或增大 payload，可在 PeripheralSPI 暴露
// setUseDMA(bool) 公共方法并在此处启用。

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
    // TX with auto-ACK; 阻塞最长 ~2ms; true = ACK 收到
    bool writePacket(const uint8_t *data);
    // 组装 15 字节 payload 并发送；返回 auto-ACK 结果（true=接收端在线）
    bool sendInputFrame(uint16_t buttons, uint8_t dpad,
                        uint16_t lx, uint16_t ly,
                        uint16_t rx, uint16_t ry,
                        uint8_t lt, uint8_t rt, uint8_t inputMode);

    PeripheralSPI* spi_ = nullptr;
    SPIBaudrateProfile spiProfile_ = {};
    int8_t csPin_ = -1;
    int8_t cePin_ = -1;
    bool initialized = false;

    // 节流缓存：与 uart_link 策略一致
    uint32_t lastSentUs = 0;
    uint16_t lastButtons = 0;
    uint8_t  lastDpad = 0;
    uint16_t lastLx = 0, lastLy = 0, lastRx = 0, lastRy = 0;
    uint8_t  lastLt = 0, lastRt = 0;
    uint8_t  lastInputMode = 0xFF;

    // 接收端在线去抖状态（ACK 历史），跨核通过 Storage nrf24LinkUp 发布
    bool     linkUp = false;
    uint8_t  linkAckStreak = 0;
    uint32_t lastAckUs = 0;
};

#endif
