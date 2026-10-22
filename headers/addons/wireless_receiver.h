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

// 配对 / 切输入模式都要写 flash + 重启；收到一个模式字节不对的包就立刻重启，
// 手柄会在 PC 上反复消失/出现（时连时断）。要求连续这么多包报同一个模式才动作。
// 发送端 1~2ms 一包 -> 约 20~40ms 确认，真实切模式感觉不到，抖动包会被挡住。
#ifndef WIRELESS_MODE_CONFIRM_PACKETS
#define WIRELESS_MODE_CONFIRM_PACKETS 20
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
    bool usbHidden;     // 未配对时已执行 tud_disconnect（延迟到 preprocess，见 cpp 注释）

    // 模式切换去抖：连续 WIRELESS_MODE_CONFIRM_PACKETS 个包报同一模式才写 flash+重启
    uint8_t pendingMode;
    uint8_t pendingModeCount;

    // 包序号去重 / 丢包统计（seq 是发送端 pkt[1]，每发一包 +1）
    uint8_t lastSeq;
    bool haveSeq;
    uint32_t rxPackets;         // 收到的有效包（去重后）
    uint32_t lostPackets;       // 按 seq 跳变推算出的丢包数
    uint16_t dupPackets;        // 完全重复（auto-ACK 重传）的包数
};

// 全局单例：core0 与 core1 共享（定义在 wireless_receiver.cpp）
extern WirelessReceiver wirelessReceiver;

#endif
