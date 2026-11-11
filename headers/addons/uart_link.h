#ifndef _UART_LINK_H_
#define _UART_LINK_H_

#include "gpaddon.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

// PIO UART 引脚硬编码（HML/RP2040，GPIO6/7，与 USB0 D-/D+ 互斥）
#define UART_LINK_RX_PIN  7
#define UART_LINK_TX_PIN  6

#ifndef UART_LINK_BAUD
#define UART_LINK_BAUD 921600
#endif

// PIO 使用 PIO0 SM0(TX) + SM1(RX)；与 USB 验证器互斥，重启切换。
// blockUSB0.enabled=false 时 USBHostManager::start() 不调用 tuh_init，PIO0 空闲可用。
#define UART_LINK_PIO     pio0
#define UART_LINK_SM_TX   0
#define UART_LINK_SM_RX   1

// ---- frame protocol (shared with ESP32 side) ----
// 帧格式: 0xAA + version + type + len + payload + CRC16(CCITT-FALSE)
// STATUS 帧 payload: [0]=socdMode(固定0) [1]=dpadMode(固定0) [2]=inputMode
//                    （BLE 设备类型选择依据） [3]=radioMode(0=BLE,1=nRF24)，len=4，帧长 10
//                    ESP32 侧兼容 len=3 旧帧（radioMode 默认 0=BLE）
// LINK_STATUS 帧（ESP32 -> Pico，type=0x0B，len=1，帧长 7）:
//   [0]=bleConnected(主机已连接)
// 发射端 RP2040 发送 INPUT/STATUS；接收方向仅处理 LINK_STATUS（驱动未配对灯效），
// ACK/CONFIG/CONFIG_ACK/LED/ESP_SAVE/ESP_LOAD/ESP_LOAD_REQ/MUTE 帧一律不处理，
// 仅做帧同步与 CRC 校验后丢弃。
#define LINK_FRAME_MAGIC      0xAA
#define LINK_FRAME_VERSION    1
#define LINK_FRAME_TYPE_INPUT 0x01
#define LINK_FRAME_TYPE_ACK   0x02
#define LINK_FRAME_TYPE_STATUS 0x03
#define LINK_FRAME_TYPE_CONFIG 0x04
#define LINK_FRAME_TYPE_CONFIG_ACK 0x05
#define LINK_FRAME_TYPE_LED   0x06
#define LINK_FRAME_TYPE_ESP_SAVE 0x07
#define LINK_FRAME_TYPE_ESP_LOAD_REQ 0x08
#define LINK_FRAME_TYPE_ESP_LOAD 0x09
#define LINK_FRAME_TYPE_MUTE 0x0A
#define LINK_FRAME_TYPE_LINK_STATUS 0x0B

// INPUT 帧恒流发送间隔（微秒）：距上次发送 ≥900us 才发一帧最新快照，
// 与 2354 验证版 nRF24 恒流节拍一致；避免 free-run 主循环把 921600 链路写满。
#define UART_INPUT_INTERVAL_US    900
// STATUS 帧心跳间隔（微秒）：inputMode 变化即发，空闲时 1s 保底
#define UART_STATUS_HEARTBEAT_US  1000000
// ESP32 LINK_STATUS 心跳 100ms；超过 500ms（连续 5 帧丢失）判 ESP32 离线，
// 未配对灯效按蓝牙未连接/伴侣缺失闪烁。
#define UART_LINK_STATUS_TIMEOUT_US 500000
// TX DMA 看门狗（微秒）：单帧最长 19 字节约 206us，超过该时限仍 busy 说明
// SM 停摆或 DREQ 不再触发，abort 通道，避免永久 busy 导致链路静默死亡。
#define UART_TX_STUCK_TIMEOUT_US  20000

class UARTLinkAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void preprocess() {}
    virtual void process();
    virtual void postprocess(bool sent);
    virtual std::string name() { return "UARTLinkAddon"; }
    virtual void reinit() {}
private:
    // 返回 true=DMA 已启动发送，false=被 busy/未初始化跳过
    bool sendInputFrame(uint16_t buttons, uint8_t dpad,
                        uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry,
                        uint8_t lt, uint8_t rt);
    // STATUS 帧: inputMode（BLE 设备类型）+ radioMode（0=BLE,1=nRF24），
    // 其余字段固定默认值
    bool sendStatusFrame(uint8_t inputMode, uint8_t radioMode);
    void handleRxByte(uint8_t b);
    // LINK_STATUS 帧（ESP32 BLE 主机连接状态）：刷新时间戳并发布到 Storage
    void handleLinkStatus();

    bool initialized = false;
    uint32_t lastInputSentUs;
    uint32_t lastStatusSentUs;
    uint32_t lastTxStartUs;       // 最近一次启动 TX DMA 的时间（看门狗用）
    uint8_t lastInputMode;
    uint8_t lastRadioMode;
    // 最近一次收到合法 LINK_STATUS 帧的时间（0=从未收到/已超时），驱动未配对灯效
    uint32_t lastLinkStatusUs;

    // ---- PIO/DMA 状态 ----
    int dmaChanTx = -1;           // DMA 通道号
    uint txOffset = 0;            // uart_tx 程序在 PIO0 中的加载地址
    uint rxOffset = 0;            // uart_rx 程序在 PIO0 中的加载地址
    // DMA 发送缓冲（成员变量保证 DMA 传输期间有效）
    // DMA 传 uint32_t，每个字节放一个 word 的低 8 位；最大帧 INPUT=19 字节
    alignas(4) uint32_t txBuffer[19];

    // ---- RX 帧同步 ----
    uint8_t rxState;    // 0 idle, 1 ver, 2 type, 3 len, 4 payload, 5 crcLo, 6 crcHi
    uint8_t rxType;
    uint8_t rxLen;
    uint8_t rxIdx;
    uint8_t rxPayload[16];
    uint16_t rxCrcCalc;
    uint8_t rxCrcLo;
};

#endif
