// esp32_lite — GP2040-CE 发射端附属 ESP32-S3 固件（精简版，无屏幕/菜单）
//
// 职责：
//   1. UART 接收发射端 Pico（uart_link.cpp）的 INPUT/STATUS 帧
//   2. 将手柄状态经 nRF24 定频转发给接收端（15 字节定长包，与正式版一致）
//   3. 板载 WS2812（GPIO21）状态指示灯
//   4. nRF / BLE 输出后端互斥切换：STATUS 帧 inputMode==BLE_INPUT_MODE(19)
//      时切蓝牙（关 nRF），其余模式（含未知 0xFF）按无线模式传输
//
// BLE 蓝牙手柄：模拟 Xbox Series X 蓝牙手柄（ESP32-BLE-CompositeHID + NimBLE，必需库），
//             实现见 bleBegin/bleStop/bleTask
// 协议：0xAA + version + type + len + payload + CRC16/CCITT-FALSE
//       与 GP-combine esp32.ino / Pico 侧 uart_link.cpp 逐字节一致
// ============================================================================

#include <SPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nrf24.h"

// ===== BLE 蓝牙手柄（模拟 Xbox Series X，BLE Composite HID + NimBLE）=====
// 必需库（均安装到"速写本/sketchbook"的 libraries 目录）：
//   菜单：文件 → 首选项 → "项目文件夹位置(sketchbook)" 所示路径下的 libraries\
//   - ESP32-BLE-CompositeHID（含 XboxGamepadDevice 的版本，如 Mystfit/felixstorm）
//     该库为旧格式（无 library.properties），不会出现在"库管理器已安装"列表，
//     用 项目→包含库→添加.ZIP库 安装即可，正常参与编译。
//   - NimBLE-Arduino（h2zero，>= 2.5.1，CompositeHID 兼容；2.1.2 在 ESP32 core 3.x 下崩溃）
//   - Callback（tomstewart89，提供 <Callback.h>）
// 注意：这里必须【无条件 #include】，不能用 #if __has_include(...) 守卫！
//   Arduino IDE 2.x 靠预处理器发现库依赖：守卫里的 include 在库尚未入搜索路径时
//   会被预处理器跳过，导致库永远不被发现收录（文件明明在 libraries\ 却报找不到）。
#include <NimBLEDevice.h>
#include <BleCompositeHID.h>
#include <XboxGamepadDevice.h>
#include "esp_mac.h"

// ===== UART（对接 Pico 侧 uart_link.cpp）=====
#define ESP_RX 44       // GPIO44, silkscreen "RX" <- Pico TX
#define ESP_TX 43       // GPIO43, silkscreen "TX" -> Pico RX
#define UART_BAUD 921600

// 链路串口：独立构造一个挂在 UART0 的 HardwareSerial（GPIO43 TX/GPIO44 RX），
// 不依赖全局 Serial/Serial0 —— Arduino-ESP32 3.x 开启 USB CDC On Boot 时全局 Serial
// 为 HWCDC（begin 不接受引脚参数），2.x 部分板型也不提供 Serial0 符号。
#include <HardwareSerial.h>
static HardwareSerial LinkSerial(0);
#define LINK_SERIAL LinkSerial


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

// ---- 手柄状态（INPUT 帧 → radioTask / BLE 共用数据源）----
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

// ---- BLE 蓝牙手柄（模拟 Xbox Series X，BLE HID）----
// 生命周期：NimBLE 栈在首次进入蓝牙模式时惰性创建、整个上电周期复用（库 begin() 内部
// 一次性 NimBLEDevice::init，反复 init/deinit 不稳定）；切回无线时仅停广播/断开连接，
// 不销毁对象。bleConnected 由 bleTask 维护，供 LED 显示。
// 重连后无输出根因：ESP32 重启后 onConnect 立即置 connected=true，但 CCCD 恢复
// (ble_gatts_bonding_restored) 是认证完成后异步发生——bleTask 在 CCCD 恢复前就调
// notify() 导致静默失败。修复：onAuthenticationComplete 回调中重置 bleForceSend，
// bleTask 连接后延迟 1s 再发首帧，等 CCCD 恢复完毕。
volatile bool bleRunning = false;
volatile bool bleConnected = false;
// bleDesired：输出模式切换器对 BLE 栈的"期望运行"请求；bleRunning 是 bleTask
// 完成实际启停后的"实际运行"状态。启停动作（NimBLE init 数百 ms / 断连等待）
// 必须在 core0 的 bleTask 自身上下文执行——若在 core1 loopTask 的 UART 接收
// 路径里直接 bleBegin()，会与已在 core0 运行的 bleTask/NimBLE host 跨核并发
// 初始化，造成 loop 阻塞→LED 停在 setup 初始红灯、连接参数请求时序错乱。
volatile bool bleDesired = false;
// 连接间隔是否已达高速（<=6 unit≈133Hz 容量）：bleTask 治理循环维护，供 LED 显示。
volatile bool bleIntervalFast = false;
void bleBegin();
void bleStop();

static BleCompositeHID   *bleHid = nullptr;
static XboxGamepadDevice *blePad = nullptr;
static bool      bleStackStarted = false;   // NimBLE 栈是否已 begin()
static uint32_t  bleStackStartMs = 0;       // begin() 时刻（用于避开初始化竞态）
static bool      bleForceSend    = true;    // （重）连接/恢复后强制发一帧

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
        // BLE 模式下由 bleTask 读取这些全局并按 on-change 编码发送 HID report
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
        // 仅发请求：bleBegin()（NimBLE init 数百 ms）由 core0 bleTask 在其自身
        // 任务上下文完成，严禁在本 loopTask(core1) 里直接调——跨核并发初始化
        // NimBLE 会阻塞 loop（LED 卡死在红灯）并扰乱连接参数协商。
        bleDesired = true;
    } else {
        // 切无线：请求 bleTask 停广播/断开主机（异步）；nRF 这边立即恢复。
        // 切换瞬间 BLE 断连（最长约 100ms）与 nRF 发送短暂并存，同频干扰至多丢
        // 几个包，链路历史阈值(10/100)可吸收。
        bleDesired = false;
        radio.powerUp();
        radio.setChannel(NRF24_CHANNEL);  // 与接收端一致：固定信道
        radio.resetLink();                // 清 TX FIFO/STATUS，防止断电前残留包重发
        delay(2);                         // nRF24 上电后需 ~1.5ms 进入 Standby
        radioUp = true;
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
// BLE 蓝牙手柄（模拟 Xbox Series X 手柄；库 ESP32-BLE-CompositeHID + NimBLE）
// 使用 XboxSeriesXControllerDeviceConfiguration：PID=0x0B13（Series X），HID 描述符
// 含正确的 Share 按钮（1914 描述符），Windows 10/11 原生识别为 Xbox 手柄。
// ============================================================================

// GP2040 按键位（低 16 位）→ Xbox 按钮
//   bit: B1 B2 B3 B4 L1 R1 L2 R2 S1 S2 L3 R3 A1 A2 A3 A4(Fn)
// 位14=A3、位15=Function 热键(AUX)不上发；L2/R2(位6/7)为数字扳机，
// 模拟量走 lastLT/lastRT；A2(位13)=Share。
struct BleBtnMap { uint16_t gp; uint16_t xb; };
static const BleBtnMap bleBtnTable[] = {
    { 1u << 0,  XBOX_BUTTON_A },       // B1 = A
    { 1u << 1,  XBOX_BUTTON_B },       // B2 = B
    { 1u << 2,  XBOX_BUTTON_X },       // B3 = X
    { 1u << 3,  XBOX_BUTTON_Y },       // B4 = Y
    { 1u << 4,  XBOX_BUTTON_LB },      // L1 = LB
    { 1u << 5,  XBOX_BUTTON_RB },      // R1 = RB
    { 1u << 8,  XBOX_BUTTON_SELECT },  // S1 = Back/View
    { 1u << 9,  XBOX_BUTTON_START },   // S2 = Start/Menu
    { 1u << 10, XBOX_BUTTON_LS },      // L3
    { 1u << 11, XBOX_BUTTON_RS },      // R3
    { 1u << 12, XBOX_BUTTON_HOME },    // A1 = Guide/Home
};

// Pico 经 UART 发来的 dpad 是 GP2040 位掩码（headers/gamepad/GamepadState.h）：
//   bit0=上(GAMEPAD_MASK_UP) bit1=下 bit2=左 bit3=右，可任意组合（非 0~8 的 hat 值！）。
// 逐方向转成 XboxDpadFlags 位标志后交给库 pressDPadDirectionFlag：
// 库内部负责滤除对向（上下/左右同按）并映射为 hat 值 0~8，与参考项目
// esp32s3_hitbox_solution 的 XinputToXBOXDpad 累积方案一致。
static uint8_t dpadMaskToXboxFlags(uint8_t mask) {
    uint8_t f = XboxDpadFlags::NONE;
    if (mask & 0x01) f |= XboxDpadFlags::NORTH;   // 上
    if (mask & 0x02) f |= XboxDpadFlags::SOUTH;   // 下
    if (mask & 0x04) f |= XboxDpadFlags::WEST;    // 左
    if (mask & 0x08) f |= XboxDpadFlags::EAST;    // 右
    return f;
}

// 将手柄状态写入 Xbox 报告（不发送）。坐标：0..65535(中心0x8000) → -32768..32767，
// Y 轴取反（上为正，与 GP2040 XInput 驱动一致）；扳机 0..255 → 0..1023。
static void bleApplyState(uint16_t btns, uint8_t dpad,
                          uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry,
                          uint8_t lt, uint8_t rt) {
    for (const auto &m : bleBtnTable) {
        if (btns & m.gp) blePad->press(m.xb);
        else             blePad->release(m.xb);
    }
    if (btns & (1u << 13)) blePad->pressShare();   // A2 = Capture/Share
    else                   blePad->releaseShare();

    blePad->pressDPadDirectionFlag((XboxDpadFlags)dpadMaskToXboxFlags(dpad));

    // 数字 L2/R2 按下且无模拟量时补满压（与 GP2040 XInput 驱动逻辑一致）
    uint16_t ltV = ((btns & (1u << 6)) && lt == 0) ? 255 : lt;
    uint16_t rtV = ((btns & (1u << 7)) && rt == 0) ? 255 : rt;
    blePad->setLeftTrigger ((uint16_t)((uint32_t)ltV * XBOX_TRIGGER_MAX / 255));
    blePad->setRightTrigger((uint16_t)((uint32_t)rtV * XBOX_TRIGGER_MAX / 255));

    blePad->setLeftThumb ((int16_t)((int32_t)lx - 0x8000), (int16_t)(0x7FFF - ly));
    blePad->setRightThumb((int16_t)((int32_t)rx - 0x8000), (int16_t)(0x7FFF - ry));
}

// 让 BLE 公开地址相对出厂 BT MAC 只置位"本地管理位(LAA)"：
//   - 与旧固件（PID 0x028E/1708 描述符、GENERIC_HID appearance）地址不同 → 主机把本设备当
//     全新设备重新枚举，彻底清除 Windows 按 MAC 缓存的旧 GATT/HID/友好名（2026-09-11 修正
//     appearance 0x03C0→0x03C4 时偏移由 -2 改为 -3，强制重装 Xbox 设备节点）；
//   - 仍由芯片 efuse MAC 派生，每颗芯片唯一且跨重启稳定 → 不影响同版固件配对后的重连绑定。
// 必须在 NimBLEDevice::init() 之前调用（库 begin() 内部才 init，故首次 bleBegin 调用即可）。
static void bleSetUniqueAddress() {
    uint8_t bt[6];
    esp_read_mac(bt, ESP_MAC_BT);
    uint8_t base[6];
    memcpy(base, bt, 6);
    base[0] = (uint8_t)((base[0] & 0xFC) | 0x02);  // 置 LAA 位，保证与出厂公网 OUI 不同
    // ESP32 的 BT MAC = base MAC 末字节 +2：base 末字节取 bt[5]-3，则最终 BT 末字节=bt[5]-1，
    // 与上一版固件(-2→bt[5])错开 1，确保 Windows 不会复用旧的设备节点。
    base[5] = (uint8_t)(bt[5] - 3);
    esp_base_mac_addr_set(base);
}

void bleBegin() {
    if (!bleStackStarted) {
        bleSetUniqueAddress();   // 必须在 NimBLE init 前改变身份地址
        // 首次进入：模拟 Xbox Series X 蓝牙手柄（CompositeHID + NimBLE）。
        auto *cfg = new XboxSeriesXControllerDeviceConfiguration();
        BLEHostConfiguration hostCfg = cfg->getIdealHostConfiguration();
        // Series X：VID=0x045E / PID=0x0B13 / BCD=0x0509，Windows 10/11 原生识别。
        cfg->setAutoReport(false);                 // 由我们按 on-change 手动 send
        blePad = new XboxGamepadDevice(cfg);
        // 设备名/厂商名对齐真 Xbox Series X 蓝牙手柄：Windows 各层（蓝牙设置、
        // HID product、测试工具）显示的设备名来自 GAP Device Name，名为
        // "GP2040-CE-BLE" 时会显示成 XINPUT IG/Standard Gamepad 等通用节点。
        bleHid = new BleCompositeHID("Xbox Wireless Controller", "Microsoft Corporation", 100);
        bleHid->addDevice(blePad);
        bleHid->begin(hostCfg);
        bleStackStarted = true;
        bleStackStartMs = millis();
        bleForceSend = true;
    } else if (NimBLEDevice::isInitialized()) {
        // 再次进入：栈仍在，用同一个传统广播对象恢复广播（含名字/服务/appearance）
        NimBLEDevice::getServer()->getAdvertising()->start();
        bleForceSend = true;
    }
}

void bleStop() {
    bleConnected = false;
    if (bleStackStarted && NimBLEDevice::isInitialized()) {
        if (bleHid && blePad && bleHid->isConnected()) {
            // dpad 传 0（掩码全空）；旧代码传 8 是 HID hat 语义残留——掩码 8=bit3
            // 会被 dpadMaskToXboxFlags 解释成 EAST(右)，断连前误发一帧右键。
            bleApplyState(0, 0, 0x8000, 0x7FFF, 0x8000, 0x7FFF, 0, 0);
            blePad->sendGamepadReport();
        }
        NimBLEDevice::getServer()->getAdvertising()->stop();
        // 断开已连接主机（best-effort）
        NimBLEServer *srv = NimBLEDevice::getServer();
        if (srv) {
            for (int i = 0; i < 5 && srv->getConnectedCount() > 0; ++i) {
                NimBLEConnInfo ci = srv->getPeerInfo((uint8_t)0);
                srv->disconnect(ci);
                vTaskDelay(20 / portTICK_PERIOD_MS);
            }
        }
    }
}

// 配置并重启广播，确保携带设备名。
// 库的 taskServer 启动广播时可能未写入设备名，导致主机搜索列表读不到名字。
// NimBLE 传统广播单例：setName 后需 stop()+start() 才能让新广播数据生效。
static void bleRestartAdvertisingWithName() {
    if (!NimBLEDevice::isInitialized()) return;
    NimBLEAdvertising *adv = NimBLEDevice::getServer()->getAdvertising();
    adv->setName("Xbox Wireless Controller"); // 与 GAP Device Name 一致（真手柄名）
    adv->stop();
    adv->start();
}

// BLE 任务：蓝牙模式下连接后按 on-change 发送 HID report
// 等待 onAuthenticationComplete（CCCD 恢复完毕）后再发首帧，避免 notify 静默失败。
void bleTask(void *) {
    bool wasConnected = false;
    bool wasReady = false;
    bool advNamed = false;
    uint32_t connMs = 0;      // 连接时刻
    uint32_t lastParamMs = 0; // 上次请求高速连接参数时刻
    uint32_t lastPollMs = 0;  // 上次间隔轮询时刻
    // on-change 发送：缓存上次发送的状态，变化时才发（含 bleForceSend 强制发）
    uint16_t sBtns = 0xFFFF, sLX = 0xFFFF, sLY = 0xFFFF, sRX = 0xFFFF, sRY = 0xFFFF;
    uint8_t  sDpad = 0xFF, sLT = 0xFF, sRT = 0xFF;
    for (;;) {
        // 状态收敛：NimBLE 栈的启停一律在本任务(core0)上下文执行。
        if (bleDesired != bleRunning) {
            if (bleDesired) {
                bleBegin();
                bleRunning = true;
            } else {
                bleStop();
                bleRunning = false;
            }
            wasConnected = false;
            wasReady = false;
            advNamed = false;
            bleIntervalFast = false;
        }

        if (bleRunning && bleHid) {
            bool conn = bleHid->isConnected();
            if (conn != bleConnected) {
                bleConnected = conn;
                if (conn) {
                    connMs = millis();
                    lastParamMs = 0;
                    lastPollMs = 0;
                    bleIntervalFast = false;
                    bleForceSend = true;
                } else {
                    // 仅在"已连接→断开"边沿恢复广播。必须以 bleRunning 为前提：
                    // applyOutputMode 切回无线时先置 bleRunning=false 再 bleStop()，
                    // 若此处无条件复活广播，切走后 ESP32 仍在 BLE 广播、Windows 自动
                    // 重连形成"无线模式下的幽灵连接"。
                    if (wasConnected && bleRunning && NimBLEDevice::isInitialized()) {
                        NimBLEDevice::getServer()->getAdvertising()->start();
                    }
                }
            }
            wasConnected = conn;

            // 认证/CCCD 恢复就绪检测：onAuthenticationComplete 后 isReady()=true
            bool ready = bleHid->isReady();
            if (ready && !wasReady) {
                bleForceSend = true;
                lastParamMs = millis();
                bleHid->requestFastConnectionParams();
            }
            wasReady = ready;

            // 栈首次就绪后：补齐广播设备名（一次性）
            if (!conn && !advNamed && NimBLEDevice::isInitialized()
                && (int32_t)(millis() - bleStackStartMs) > 2000) {
                bleRestartAdvertisingWithName();
                advNamed = true;
            }

            if (conn) {
                uint32_t sinceConn = (uint32_t)(millis() - connMs);

                // 持续连接间隔治理：每 500ms 读一次真实间隔，一旦 >6 unit 立刻按
                // 750ms 节奏重请 (6,6)；回到 <=6 后转为 3s 巡检，掉速即恢复请求。
                NimBLEServer *srv = NimBLEDevice::getServer();
                uint32_t nowMs = millis();
                if (srv && srv->getConnectedCount() > 0 &&
                    nowMs - lastPollMs >= 500) {
                    lastPollMs = nowMs;
                    NimBLEConnInfo ci = srv->getPeerInfo(0);
                    uint16_t itv = ci.getConnInterval();   // 单位 1.25ms
                    bleIntervalFast = (itv >= 1 && itv <= 6);
                    if (!bleIntervalFast && nowMs - lastParamMs >= 750) {
                        lastParamMs = nowMs;
                        bleHid->requestFastConnectionParams();
                    }
                }

                // 连接后等待认证/CCCD 恢复（最长 2s 超时，兼容无认证连接）
                if (!ready && sinceConn < 2000) {
                    vTaskDelay(8 / portTICK_PERIOD_MS);
                    continue;
                }

                uint16_t btns = lastButtons;
                uint8_t  dpad = lastDpad;
                uint16_t lx = lastLX, ly = lastLY, rx = lastRX, ry = lastRY;
                uint8_t  lt = lastLT, rt = lastRT;

                // on-change 发送：状态变化或强制发时才调 sendGamepadReport
                if (bleForceSend || btns != sBtns || dpad != sDpad ||
                    lx != sLX || ly != sLY || rx != sRX || ry != sRY ||
                    lt != sLT || rt != sRT) {
                    bleApplyState(btns, dpad, lx, ly, rx, ry, lt, rt);
                    blePad->sendGamepadReport();
                    sBtns = btns; sDpad = dpad;
                    sLX = lx; sLY = ly; sRX = rx; sRY = ry;
                    sLT = lt; sRT = rt;
                    bleForceSend = false;
                }
                // 发送节拍 5ms：on-change 模式下大部分时间无变化不发包，
                // 但 5ms 轮询保证变化后延迟 <=5ms 即发出。
                vTaskDelay(5 / portTICK_PERIOD_MS);
            } else {
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }
        } else {
            vTaskDelay(20 / portTICK_PERIOD_MS);
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
//   无线+链路正常=绿 / 无线+链路丢失=红 / 蓝牙+广播中=蓝慢闪(1Hz)
//   蓝牙+已连接+间隔达标=蓝常亮 / 蓝牙+已连接但间隔未达标(47Hz指纹)=蓝快闪(4Hz)
//   蓝牙+栈未运行=紫(异常指纹，正常不应出现)
// ============================================================================
void updateLed() {
    unsigned long now = millis();
    if (now - lastLedMs < 100) return;
    lastLedMs = now;

    uint8_t r = 0, g = 0, b = 0;
    if (outputMode == OUTPUT_BLE) {
        if (!bleRunning) {
            r = 40; b = 40;                 // 异常：BLE 模式但栈未运行（诊断指纹）
        } else if (bleConnected) {
            if (bleIntervalFast) {
                r = 0; g = 0; b = 60;       // 已连接+间隔达标(125Hz)：蓝色常亮
            } else {
                bool on = (now / 250) % 2 == 0;  // 已连接但间隔未达标(47Hz)：蓝快闪
                if (on) { r = 0; g = 0; b = 60; }
            }
        } else {
            bool on = (now / 500) % 2 == 0; // 广播中：1Hz 蓝色慢闪
            if (on) { r = 0; g = 0; b = 48; }
        }
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
    // 降频 80MHz 省电：BLE 控制器有独立基带时钟，降频不影响蓝牙；
    // nRF24 SPI 在 80MHz 下软件层开销可接受（HSPI 硬件 SPI 不依赖 CPU 频率）。
    setCpuFrequencyMhz(80);

    // 上电先点红，进入 loop 后由状态机接管
    ledWrite(48, 0, 0);
    ledR = 48;

    nrfSpi.begin(12, 13, 11, -1); // SCK=12, MISO=13, MOSI=11 (HSPI；CSN=10/CE=9 由驱动管理)
    radio.begin(nrfSpi, NRF_CSN, NRF_CE);

    LINK_SERIAL.begin(UART_BAUD, SERIAL_8N1, ESP_RX, ESP_TX);
    radioUp = true;  // 默认无线模式；收到 STATUS inputMode=19 后切蓝牙

    xTaskCreatePinnedToCore(radioTask, "radio", 4096, NULL, 1, NULL, 0); // 发送跑在核0
    xTaskCreatePinnedToCore(bleTask, "ble", 8192, NULL, 1, NULL, 0);     // BLE 任务常驻核0
}

void loop() {
    while (LINK_SERIAL.available()) {
        handleRxByte((uint8_t)LINK_SERIAL.read());
    }
    updateLed();
}
