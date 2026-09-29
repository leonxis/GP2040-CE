#ifndef _UART_LINK_H_
#define _UART_LINK_H_

#include "gpaddon.h"
#include "BoardConfig.h"

#ifndef DEFAULT_BLUETOOTH_LINK_ENABLED
#define DEFAULT_BLUETOOTH_LINK_ENABLED 0
#endif

#ifndef UART_LINK_TX_PIN
#define UART_LINK_TX_PIN 8
#endif

#ifndef UART_LINK_RX_PIN
#define UART_LINK_RX_PIN 9
#endif

#ifndef UART_LINK_BAUD
#define UART_LINK_BAUD 921600
#endif

// ---- frame protocol (shared with ESP32 side) ----
// 帧格式: 0xAA + version + type + len + payload + CRC16(CCITT-FALSE)
// STATUS 帧 payload: [0]=socdMode(固定0) [1]=dpadMode(固定0) [2]=inputMode
//                    （BLE 设备类型选择依据），len=3，帧长 9
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

// STATUS 帧心跳间隔（微秒）：inputMode 变化即发，空闲时 1s 保底
#define UART_STATUS_HEARTBEAT_US  1000000
// ESP32 LINK_STATUS 心跳 100ms；超过 500ms（连续 5 帧丢失）判 ESP32 离线，
// 未配对灯效按蓝牙未连接/伴侣缺失闪烁。
#define UART_LINK_STATUS_TIMEOUT_US 500000

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
    void sendInputFrame(uint16_t buttons, uint8_t dpad,
                        uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry,
                        uint8_t lt, uint8_t rt);
    // STATUS 帧: 仅 inputMode 真实，其余字段固定默认值（ESP32 据此选 BLE 设备类型）
    void sendStatusFrame(uint8_t inputMode);
    void handleRxByte(uint8_t b);
    // LINK_STATUS 帧（ESP32 BLE 主机连接状态）：刷新时间戳并发布到 Storage
    void handleLinkStatus();
    bool initialized = false;
    uint32_t lastStatusSentUs;
    uint8_t lastInputMode;
    // 最近一次收到合法 LINK_STATUS 帧的时间（0=从未收到/已超时），驱动未配对灯效
    uint32_t lastLinkStatusUs;
    uint8_t rxState;    // 0 idle, 1 ver, 2 type, 3 len, 4 payload, 5 crcLo, 6 crcHi
    uint8_t rxType;
    uint8_t rxLen;
    uint8_t rxIdx;
    uint8_t rxPayload[16];
    uint16_t rxCrcCalc;
    uint8_t rxCrcLo;
};

#endif
