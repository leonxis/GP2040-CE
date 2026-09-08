// ============================================================================
// esp32_lite — GP2040-CE 发射端附属 ESP32-S3 固件（精简版，无屏幕/菜单）
//
// 职责：
//   1. UART 接收发射端 Pico（uart_link.cpp）的 INPUT/STATUS 帧
//   2. 将手柄状态经 nRF24 定频转发给接收端（15 字节定长包，与正式版一致）
//   3. 板载 WS2812（GPIO21）状态指示灯
//   4. nRF / BLE 输出后端互斥切换：STATUS 帧 inputMode==BLE_INPUT_MODE(19)
//      时切蓝牙（关 nRF），其余模式（含未知 0xFF）按无线模式传输
//
// 预留：BLE 蓝牙手柄骨架（bleBegin/bleStop/bleTask 空实现，见 TODO(蓝牙)）
// 协议：0xAA + version + type + len + payload + CRC16/CCITT-FALSE
//       与 GP-combine esp32.ino / Pico 侧 uart_link.cpp 逐字节一致
// ============================================================================

#include <SPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nrf24.h"

// ===== UART（对接 Pico 侧 uart_link.cpp）=====
#define ESP_RX 44       // GPIO44, silkscreen "RX" <- Pico TX
#define ESP_TX 43       // GPIO43, silkscreen "TX" -> Pico RX
#define UART_BAUD 921600

// 链路串口：Arduino-ESP32 3.x 开启 USB CDC On Boot 时，Serial 为 HWCDC（USB），
// 其 begin() 不接受引脚参数（编译报错 HWCDC::begin(int,SerialConfig,int,int)）。
// 与 Pico 通信必须走硬件 UART0 = Serial0（ESP32-S3 默认引脚即 GPIO43 TX/GPIO44 RX）；
// 2.x 核心下 Serial 本身即 UART0，行为等价。
#define LINK_SERIAL Serial0

// ===== nRF24L01（HSPI 总线）：CE=9, CSN=10, MOSI=11, SCK=12, MISO=13 =====
#define NRF_CSN 10
#define NRF_CE  9

// ===== 板载 WS2812 状态灯 =====
#define LED_PIN 21

// 灯珠物理线序为 RGB（非 WS2812 常见的 GRB）：Arduino 核心 neopixelWrite(pin,r,g,b)
// 固定按 GRB 时序发线（线上字节 = G,R,B），本板灯珠按 R,G,B 解释，直接调用会红绿互换
// （链路正常显红、链路丢失显绿）。此封装保持 RGB 语义入参，内部交换 R/G 修正线序。
void ledWrite(uint8_t r, uint8_t g, uint8_t b) {
    neopixelWrite(LED_PIN, g, r, b);   // 线上 GRB → RGB 灯珠：R=入参r, G=入参g, B=入参b
}

// ===== 帧协议常量（精简：仅保留需要的帧类型）=====
#define FRAME_MAGIC       0xAA
#define FRAME_VERSION     1
#define FRAME_TYPE_INPUT  0x01   // Pico -> ESP32: 手柄输入
#define FRAME_TYPE_ACK    0x02   // ESP32 -> Pico: 输入确认（Pico 链路指示依赖此帧）
#define FRAME_TYPE_STATUS 0x03   // Pico -> ESP32: 模式状态（仅取 inputMode 字段）

// 蓝牙模式标识：与 wireless-tx 分支 proto/enums.proto 后续新增的 INPUT_MODE_BLE = 19
// 保持一致（当前枚举最大 INPUT_MODE_XINPUTB = 18，CONFIG = 255）
#define BLE_INPUT_MODE 19

// ---- 输出后端互斥切换 ----
enum OutputBackend : uint8_t { OUTPUT_NRF = 0, OUTPUT_BLE = 1 };
volatile uint8_t outputMode = OUTPUT_NRF;  // 初始无线；inputMode 未知(0xFF)也按无线处理

// ---- nRF24 无线状态 ----
NRF24 radio;
SPIClass nrfSpi(HSPI);
volatile bool radioUp = false;
uint8_t radioSeq = 0;
// link quality: ACK results over the last 100 packets
volatile uint8_t radioHist[100];
uint8_t radioHistIdx = 0;
volatile uint8_t radioHistOk = 0;
volatile bool radioLinked = false;

// ---- 手柄状态（INPUT 帧 → radioTask / 后续 BLE 共用数据源）----
volatile uint16_t lastButtons = 0;
volatile uint8_t lastDpad = 0;
volatile uint16_t lastLX = 0x8000, lastLY = 0x8000, lastRX = 0x8000, lastRY = 0x8000;
volatile uint8_t lastLT = 0, lastRT = 0;

// ---- STATUS 帧解析（仅 inputMode）----
volatile uint8_t stInputMode = 0;
volatile bool stInputModeValid = false;

// ---- WS2812 状态灯 ----
uint8_t ledR = 0, ledG = 0, ledB = 0;   // 当前已写颜色缓存
unsigned long lastLedMs = 0;

// ---- BLE 蓝牙骨架（预留，本轮空实现）----
volatile bool bleRunning = false;
volatile bool bleConnected = false;     // TODO(蓝牙): 由 BLE 事件回调维护，供 LED 显示
void bleBegin();
void bleStop();

// ============================================================================
// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) — 与 uart_link.cpp 一致
// ============================================================================
static uint16_t crc16_update(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

// ============================================================================
// UART TX：ACK（Pico 侧 lastAckTime 链路指示依赖）
// ============================================================================
void sendAck() {
    uint8_t frame[7];
    frame[0] = FRAME_MAGIC;
    frame[1] = FRAME_VERSION;
    frame[2] = FRAME_TYPE_ACK;
    frame[3] = 1;
    frame[4] = 0x01;
    uint16_t crc = 0xFFFF;
    for (int i = 1; i <= 4; i++) crc = crc16_update(crc, frame[i]);
    frame[5] = (uint8_t)(crc & 0xFF);
    frame[6] = (uint8_t)(crc >> 8);
    LINK_SERIAL.write(frame, sizeof(frame));
}

// ============================================================================
// UART RX：INPUT 帧 → 更新手柄状态全局（radioTask / BLE 数据源）
// ============================================================================
void onInputFrame(uint8_t *payload, uint8_t len) {
    if (len >= 3) {
        uint16_t buttons = payload[0] | ((uint16_t)payload[1] << 8);
        uint8_t dpad = payload[2];
        lastButtons = buttons;
        lastDpad = dpad;
        if (len >= 13) { // full state: sticks + triggers
            lastLX = payload[3] | ((uint16_t)payload[4] << 8);
            lastLY = payload[5] | ((uint16_t)payload[6] << 8);
            lastRX = payload[7] | ((uint16_t)payload[8] << 8);
            lastRY = payload[9] | ((uint16_t)payload[10] << 8);
            lastLT = payload[11];
            lastRT = payload[12];
        }
        // TODO(蓝牙): BLE 模式下在此处按 on-change 触发 HID report 发送
    }
    sendAck();
}

// ============================================================================
// 输出后端互斥切换：inputMode==19 切蓝牙，否则切无线
// ============================================================================
void applyOutputMode() {
    uint8_t desired = (stInputModeValid && stInputMode == BLE_INPUT_MODE)
                          ? OUTPUT_BLE : OUTPUT_NRF;
    if (desired == outputMode) return;

    outputMode = desired;
    if (outputMode == OUTPUT_BLE) {
        // 切蓝牙：先停 radioTask 发送（outputMode 已变 + radioUp 清零），
        // 等在途 writePacket（最多 ~2ms）结束后再断电 nRF，避免 SPI 事务交叉
        radioUp = false;
        vTaskDelay(3 / portTICK_PERIOD_MS);
        radio.powerDown();                // nRF 静默断电
        bleRunning = true;
        bleBegin();   // TODO(蓝牙): 初始化 BLE HID 并开始广播
        LINK_SERIAL.println("[mode] BLE output enabled, nRF24 powered down");
    } else {
        // 切无线：停止 BLE，nRF 恢复上电并清 FIFO，再放行 radioTask 发送
        bleRunning = false;
        bleStop();    // TODO(蓝牙): 断开连接并释放资源
        radio.powerUp();
        radio.setChannel(NRF24_CHANNEL);  // 与接收端一致：固定信道
        radio.resetLink();                // 清 TX FIFO/STATUS，防止断电前残留包重发
        delay(2);                         // nRF24 上电后需 ~1.5ms 进入 Standby
        radioUp = true;
        LINK_SERIAL.println("[mode] nRF24 output enabled, BLE stopped");
    }
}

// ============================================================================
// UART RX：STATUS 帧 → 仅取 inputMode，触发模式切换
// ============================================================================
void onStatusFrame(uint8_t *payload, uint8_t len) {
    if (len < 3) return;    // payload[2] = inputMode
    stInputMode = payload[2];
    stInputModeValid = true;
    applyOutputMode();
}

// ============================================================================
// nRF24 无线发送任务：核 0，2ms 定频（500Hz），与接收端 15 字节包格式一致
// 蓝牙模式时空转不发包（nRF/BLE 互斥）
// ============================================================================
void radioTask(void *) {
    uint32_t failCount = 0;
    for (;;) {
        if (outputMode == OUTPUT_NRF && radioUp) {
            uint8_t pkt[15];
            uint8_t mode = stInputModeValid ? stInputMode : 0xFF; // 0xFF=模式未知
            uint16_t btns = lastButtons;
            uint8_t dpad = lastDpad;
            uint16_t lx = lastLX, ly = lastLY, rx = lastRX, ry = lastRY;
            uint8_t lt = lastLT, rt = lastRT;
            pkt[0] = mode;
            pkt[1] = radioSeq++;
            pkt[2] = btns & 0xFF;
            pkt[3] = btns >> 8;
            pkt[4] = dpad;
            pkt[5] = lx & 0xFF;  pkt[6] = lx >> 8;
            pkt[7] = ly & 0xFF;  pkt[8] = ly >> 8;
            pkt[9] = rx & 0xFF;  pkt[10] = rx >> 8;
            pkt[11] = ry & 0xFF; pkt[12] = ry >> 8;
            pkt[13] = lt;
            pkt[14] = rt;
            bool acked = radio.writePacket(pkt);
            if (radioHist[radioHistIdx]) radioHistOk--;
            radioHist[radioHistIdx] = acked ? 1 : 0;
            if (acked) radioHistOk++;
            radioHistIdx = (radioHistIdx + 1) % 100;
            radioLinked = (radioHistOk >= 10);
            if (acked) {
                failCount = 0;
            } else {
                failCount++;
                if (failCount > 500) { // 连续失败看门狗：重新初始化模块
                    radio.begin(nrfSpi, NRF_CSN, NRF_CE);
                    radioUp = true;
                    failCount = 0;
                    for (int i = 0; i < 100; i++) radioHist[i] = 0;
                    radioHistOk = 0;
                }
            }
        }
        vTaskDelay(2 / portTICK_PERIOD_MS);
    }
}

// ============================================================================
// BLE 蓝牙骨架（预留，本轮空实现）
// ============================================================================
void bleBegin() {
    // TODO(蓝牙): 接入 NimBLE/ESP32-BLE-Gamepad：
    //   1) 初始化 BLE HID 设备（GATT HID 服务）并开始广播
    //   2) 注册连接/断开事件回调维护 bleConnected
    //   3) 连接后按 on-change 将 lastButtons/lastDpad/lastLX... 编码为 HID report 发送
    LINK_SERIAL.println("[ble] begin (skeleton, not implemented)");
}

void bleStop() {
    // TODO(蓝牙): 断开 BLE 连接、停止广播并释放栈资源
    bleConnected = false;
    LINK_SERIAL.println("[ble] stop (skeleton, not implemented)");
}

// BLE 空壳任务：常驻循环，仅蓝牙模式下运行（TODO(蓝牙): 填充收发逻辑）
void bleTask(void *) {
    for (;;) {
        if (bleRunning) {
            // TODO(蓝牙): 检测输入状态变化（lastButtons 等全局）按 on-change 发送 HID report
            vTaskDelay(10 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
}

// ============================================================================
// UART RX 状态机（与 uart_link.cpp 逐字节一致）
// ============================================================================
uint8_t rxState = 0; // 0 idle, 1 ver, 2 type, 3 len, 4 payload, 5 crcLo, 6 crcHi
uint8_t rxType = 0;
uint8_t rxLen = 0;
uint8_t rxIdx = 0;
uint8_t rxPayload[24];
uint16_t rxCrcCalc = 0;
uint8_t rxCrcLo = 0;

void handleRxByte(uint8_t b) {
    switch (rxState) {
        case 0:
            if (b == FRAME_MAGIC) rxState = 1;
            break;
        case 1:
            rxCrcCalc = 0xFFFF;
            rxCrcCalc = crc16_update(rxCrcCalc, b);
            rxState = (b == FRAME_VERSION) ? 2 : 0;
            break;
        case 2:
            rxType = b;
            rxCrcCalc = crc16_update(rxCrcCalc, b);
            rxState = 3;
            break;
        case 3:
            rxLen = b;
            rxCrcCalc = crc16_update(rxCrcCalc, b);
            if (rxLen > sizeof(rxPayload)) { rxState = 0; break; }
            rxIdx = 0;
            rxState = (rxLen == 0) ? 5 : 4;
            break;
        case 4:
            rxPayload[rxIdx++] = b;
            rxCrcCalc = crc16_update(rxCrcCalc, b);
            if (rxIdx == rxLen) rxState = 5;
            break;
        case 5:
            rxCrcLo = b;
            rxState = 6;
            break;
        case 6:
            if (b == (uint8_t)(rxCrcCalc >> 8) &&
                rxCrcLo == (uint8_t)(rxCrcCalc & 0xFF)) {
                if (rxType == FRAME_TYPE_INPUT) onInputFrame(rxPayload, rxLen);
                else if (rxType == FRAME_TYPE_STATUS) onStatusFrame(rxPayload, rxLen);
                // 其余帧类型（CONFIG/LED/ESP_SAVE/MUTE 等）无菜单/持久化需求，忽略
            }
            rxState = 0;
            break;
    }
}

// ============================================================================
// WS2812 状态灯：仅状态变化时刷新，100ms 节流（蓝牙慢闪由时间驱动）
//   无线+链路正常=绿 / 无线+链路丢失=红 / 蓝牙模式=蓝慢闪（广播中）
// ============================================================================
void updateLed() {
    unsigned long now = millis();
    if (now - lastLedMs < 100) return;
    lastLedMs = now;

    uint8_t r = 0, g = 0, b = 0;
    if (outputMode == OUTPUT_BLE) {
        // TODO(蓝牙): 接入 BLE 后改为 广播=慢闪 / 已连接=常亮
        bool on = (now / 500) % 2 == 0;   // 1Hz 慢闪
        if (on) { r = 0; g = 0; b = 48; } // 蓝色（亮度压低防刺眼）
    } else if (radioLinked) {
        g = 40;                           // 绿色：链路正常
    } else {
        r = 48;                           // 红色：链路丢失
    }

    if (r != ledR || g != ledG || b != ledB) {
        ledR = r; ledG = g; ledB = b;
        ledWrite(r, g, b);
    }
}

// ============================================================================
// setup / loop
// ============================================================================
void setup() {
    // 上电先点红，进入 loop 后由状态机接管
    ledWrite(48, 0, 0);
    ledR = 48;

    LINK_SERIAL.begin(UART_BAUD, SERIAL_8N1, ESP_RX, ESP_TX);
    LINK_SERIAL.println("[lite] esp32-lite boot");

    nrfSpi.begin(12, 13, 11, -1); // SCK=12, MISO=13, MOSI=11 (HSPI；CSN=10/CE=9 由驱动管理)
    radio.begin(nrfSpi, NRF_CSN, NRF_CE);
    radioUp = true;  // 默认无线模式；收到 STATUS inputMode=19 后切蓝牙

    xTaskCreatePinnedToCore(radioTask, "radio", 4096, NULL, 1, NULL, 0); // 发送跑在核0
    xTaskCreatePinnedToCore(bleTask, "ble", 4096, NULL, 1, NULL, 0);     // BLE 骨架常驻核0
}

void loop() {
    while (LINK_SERIAL.available()) {
        handleRxByte((uint8_t)LINK_SERIAL.read());
    }
    updateLed();
}
