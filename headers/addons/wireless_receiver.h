#ifndef _WIRELESS_RECEIVER_H_
#define _WIRELESS_RECEIVER_H_

#include "BoardConfig.h"

// --- 无线接收端引脚配置（由 BoardConfig 提供，此处提供默认值便于非配置编译） ---
#ifndef WIRELESS_RX_SPI_INST
#define WIRELESS_RX_SPI_INST spi1
#endif

#ifndef WIRELESS_RX_CE_PIN
#define WIRELESS_RX_CE_PIN 14
#endif
#ifndef WIRELESS_RX_CSN_PIN
#define WIRELESS_RX_CSN_PIN 9
#endif
#ifndef WIRELESS_RX_SCK_PIN
#define WIRELESS_RX_SCK_PIN 10
#endif
#ifndef WIRELESS_RX_MOSI_PIN
#define WIRELESS_RX_MOSI_PIN 11
#endif
#ifndef WIRELESS_RX_MISO_PIN
#define WIRELESS_RX_MISO_PIN 12
#endif
#ifndef WIRELESS_RX_LED_PIN
#define WIRELESS_RX_LED_PIN 16
#endif
#ifndef WIRELESS_RX_RGB_LED_PIN
#define WIRELESS_RX_RGB_LED_PIN -1   // >=0 时用单颗 WS2812；接收端精简板必须保持 -1（与 USB0 PIO-USB 主机冲突）
#endif

// 主循环显式调用，不再继承 GPAddon
// 核间分工（全局单例 wirelessReceiver）：
//   core0: setup() / preprocess()  —— nRF24 收包、输入注入
//   core1: process()               —— 状态灯（WS2812/GPIO LED）
// 成员归属：paired/lastPacketTime 由 core0 写、core1 读（对齐 32 位原子读，单写者无需锁）；
//           lastLedToggle/ledOn/ledState 仅 core1 访问。
class WirelessReceiver {
public:
    void setup();
    void preprocess();
    void process();
private:
    void handlePacket(const uint8_t *pkt);
    bool paired;
    uint16_t rxButtons;
    uint8_t rxDpad;
    uint16_t rxlx, rxly, rxrx, rxry;
    uint8_t rxlt, rxrt;
    uint32_t lastPacketTime;
    uint32_t lastLedToggle;
    uint32_t lastRadioReinit;
    bool ledOn;
    uint8_t ledState;   // LED 当前态：0=未知/灭, 1=红(闪烁), 2=绿(常亮)
};

// 全局单例：core0 与 core1 共享（定义在 wireless_receiver.cpp）
extern WirelessReceiver wirelessReceiver;

#endif
