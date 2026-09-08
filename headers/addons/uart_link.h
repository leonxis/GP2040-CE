#ifndef _UART_LINK_H_
#define _UART_LINK_H_

#include "gpaddon.h"
#include "BoardConfig.h"

#ifndef UART_LINK_ENABLED
#define UART_LINK_ENABLED 0
#endif

#ifndef DEFAULT_WIRELESS_LINK_ENABLED
#define DEFAULT_WIRELESS_LINK_ENABLED 0
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
// 发射端 RP2040 仅处理 INPUT 发送与 STATUS 上报；
// ACK/CONFIG/CONFIG_ACK/LED/ESP_SAVE/ESP_LOAD/ESP_LOAD_REQ/MUTE 帧一律不处理，
// 接收方向仅做帧同步与 CRC 校验后丢弃（未识别类型 break）。
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
    // STATUS 帧: 仅 inputMode 真实，其余字段全部固定默认值（协议兼容 ESP32 解析）
    void sendStatusFrame(uint8_t inputMode);
    void handleRxByte(uint8_t b);
    bool initialized = false;
    uint32_t lastSent;
    uint16_t lastButtons;
    uint8_t lastDpad;
    uint16_t lastLx, lastLy, lastRx, lastRy;
    uint8_t lastLt, lastRt;
    uint32_t lastStatusSent;
    uint8_t lastInputMode;
    uint8_t rxState;    // 0 idle, 1 ver, 2 type, 3 len, 4 payload, 5 crcLo, 6 crcHi
    uint8_t rxType;
    uint8_t rxLen;
    uint8_t rxIdx;
    uint8_t rxPayload[16];
    uint16_t rxCrcCalc;
    uint8_t rxCrcLo;
};

#endif
