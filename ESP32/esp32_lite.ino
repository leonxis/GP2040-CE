// esp32_lite — GP2040-CE 发射端附属 ESP32-S3 固件（精简版，无屏幕/菜单，纯蓝牙）
//
// 职责：
//   1. UART 接收发射端 Pico（uart_link.cpp）的 INPUT/STATUS 帧
//   2. 将手柄状态经 BLE 蓝牙发送给主机（BLE Composite HID + NimBLE）
//   3. 板载 WS2812（GPIO21）状态指示灯
//   4. inputMode(payload[2]) 始终是真实手柄模式：BLE 设备类型选择
//   5. 100ms 心跳回发 LINK_STATUS 帧：BLE 主机连接状态，供 Pico 侧环境光提示
//      （伴侣缺失红闪/蓝牙未连接蓝闪）
//   6. UART 静默看门狗：Pico 关机/蓝牙开关关闭时停 BLE 省电
//
// BLE 蓝牙手柄：按 inputMode 选择设备类型（当前仅 Xbox Series X 实现，
//             不支持的模式退化 Xbox；DualSense/NS PRO 预留），
//             ESP32-BLE-CompositeHID + NimBLE，实现见 bleBegin/bleStop/bleTask
// 协议：0xAA + version + type + len + payload + CRC16/CCITT-FALSE
//       与 Pico 侧 uart_link.cpp 逐字节一致
// ============================================================================

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
#define FRAME_TYPE_STATUS 0x03   // Pico -> ESP32: 模式状态（inputMode）
#define FRAME_TYPE_LINK_STATUS 0x0B  // ESP32 -> Pico: 连接状态心跳（见 sendLinkStatus）

// LINK_STATUS payload（len=1）：
//   [0] bleConnected：BLE 主机已连接（配对完成）
#define LINK_STATUS_HEARTBEAT_MS 100

// ---- BLE 设备类型（按 Pico inputMode 选择；当前仅 Xbox 实现，其余退化 Xbox）----
enum BlePadType : uint8_t { BLE_PAD_XBOX = 0, BLE_PAD_DUALSENSE = 1, BLE_PAD_NSPRO = 2 };

// GP2040 InputMode 编号（proto/enums.proto）→ BLE 设备类型。
// DUALSENSE/NSPRO 为预留分支：CompositeHID 库补齐对应设备后只需在此构造，
// 未实现前所有非 Xbox 模式一律退化 Xbox Series X。
// 返回 BlePadType 值；用 uint8_t 而非枚举类型作返回值：Arduino 预处理器会把
// 自动原型插到本文件枚举定义之前，原型引用枚举会编译失败（enum does not name a type）。
static uint8_t blePadTypeForInputMode(uint8_t mode) {
    switch (mode) {
        // 预留：PS4(4)/PS4B(17)/PS5(13)/P5GENERAL(16) → DualSense/DualShock 系列
        // case INPUT_MODE_PS4: case INPUT_MODE_PS4B:
        // case INPUT_MODE_PS5: case INPUT_MODE_P5GENERAL: return BLE_PAD_DUALSENSE;
        // 预留：SWITCH_PRO(15) → NS PRO
        // case INPUT_MODE_SWITCH_PRO: return BLE_PAD_NSPRO;
        case 0:   // XINPUT
        case 18:  // XINPUTB
        case 5:   // XBONE
        default:  // 其余模式（键盘/PS3/GENERIC/各 mini 主机等）退化 Xbox
            return (uint8_t)BLE_PAD_XBOX;
    }
}
volatile uint8_t stBlePadType = BLE_PAD_XBOX;  // STATUS 输入模式对应的 BLE 类型

// ---- UART 链路看门狗：Pico 静默时 BLE 休眠 ----
// Pico 切到非蓝牙模式或关机后，UART 线上再无任何字节，BLE 停广播省电。
// 活动判据=UART 线上任意字节（loop drain 即刷新），而非 CRC 合法帧：突发误码/丢包
// 恰恰证明 Pico 在线，绝不能因此休眠；Pico 切走/关机的特征是线路完全静默。
// 1000ms 无任何字节 → 停 BLE；重新收到字节（Pico 切回/重启）→ 自动恢复。
// 开机 1s 宽限等 Pico 启动首帧。
#define UART_LINK_LOST_MS   1000
#define UART_BOOT_GRACE_MS  1000
static uint32_t lastUartByteMs = 0;   // 最近一次 UART 字节活动时刻（millis），0=从未收到
static bool linkSleeping = false;     // BLE 是否已因 UART 静默休眠

// ---- 手柄状态（INPUT 帧 → BLE 数据源）----
volatile uint16_t lastButtons = 0;
volatile uint8_t lastDpad = 0;
volatile uint16_t lastLX = 0x8000, lastLY = 0x8000, lastRX = 0x8000, lastRY = 0x8000;
volatile uint8_t lastLT = 0, lastRT = 0;

// ---- STATUS 帧解析（payload[2]=inputMode）----
// 新协议帧 len=3，不做旧帧兼容。

// ---- WS2812 状态灯 ----
uint8_t ledR = 0, ledG = 0, ledB = 0;   // 当前已写颜色缓存
unsigned long lastLedMs = 0;

// ---- BLE 蓝牙手柄（模拟 Xbox Series X，BLE HID）----
// 生命周期：NimBLE 栈在首次进入蓝牙模式时惰性创建、整个上电周期复用（库 begin() 内部
// 一次性 NimBLEDevice::init，反复 init/deinit 不稳定）；休眠时仅停广播/断开连接，
// 不销毁对象。bleConnected 由 bleTask 维护，供 LED 显示。
// 重连后无输出根因：ESP32 重启后 onConnect 立即置 connected=true，但 CCCD 恢复
// (ble_gatts_bonding_restored) 是认证完成后异步发生——在 CCCD 恢复前就调 notify()
// 会静默失败。修复：bleTask 连接后轮询 isReady()（库在 onAuthenticationComplete
// 回调内置位），未就绪期间等待（最长 2s），再按 5ms 节拍发送。
volatile bool bleRunning = false;
volatile bool bleConnected = false;
// bleDesired：看门狗/初始化对 BLE 栈的"期望运行"请求；bleRunning 是 bleTask
// 完成实际启停后的"实际运行"状态。启停动作（NimBLE init 数百 ms / 断连等待）
// 必须在 core0 的 bleTask 自身上下文执行——若在 core1 loopTask 的 UART 接收
// 路径里直接 bleBegin()，会与已在 core0 运行的 bleTask/NimBLE host 跨核并发
// 初始化，造成 loop 阻塞→LED 停在初始灯、连接参数请求时序错乱。
volatile bool bleDesired = false;
// 连接间隔是否已达高速（<=6 unit≈133Hz 容量）：bleTask 治理循环维护，供 LED 显示。
volatile bool bleIntervalFast = false;
void bleBegin(uint8_t padType);
void bleStop();

static BleCompositeHID   *bleHid = nullptr;
static XboxGamepadDevice *blePad = nullptr;
static bool      bleStackStarted = false;   // NimBLE 栈是否已 begin()
static uint32_t  bleStackStartMs = 0;       // begin() 时刻（用于广播补名延时）
static uint8_t   activeBlePadType = 0xFF;   // 当前栈内已构造的 BLE 设备类型

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
// UART TX：LINK_STATUS 心跳（10Hz）——回报 BLE 主机连接状态。
// Pico 侧 uart_link 以此驱动环境光提示（ESP32 缺失红闪 / 蓝牙未连接蓝闪）；
// 超过 500ms（5 个心跳）未收到时 Pico 判 ESP32 离线。
// ============================================================================
void sendLinkStatus() {
    uint8_t frame[7];
    frame[0] = FRAME_MAGIC;
    frame[1] = FRAME_VERSION;
    frame[2] = FRAME_TYPE_LINK_STATUS;
    frame[3] = 1;
    frame[4] = bleConnected ? 1 : 0;
    uint16_t crc = 0xFFFF;
    for (int i = 1; i <= 4; i++) crc = crc16_update(crc, frame[i]);
    frame[5] = (uint8_t)(crc & 0xFF);
    frame[6] = (uint8_t)(crc >> 8);
    LINK_SERIAL.write(frame, sizeof(frame));
}

// ============================================================================
// UART RX：INPUT 帧 → 更新手柄状态全局（BLE 数据源）
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
        // BLE 模式下由 bleTask 读取这些全局并按固定 5ms 节拍编码发送 HID report
    }
}

// ============================================================================
// UART RX：STATUS 帧 → 取 inputMode(payload[2]) 更新 BLE 设备类型。
// ============================================================================
void onStatusFrame(uint8_t *payload, uint8_t len) {
    if (len < 3) return;           // payload[2]=inputMode
    stBlePadType = blePadTypeForInputMode(payload[2]);
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

// 将手柄状态写入 Xbox 报告（不发送）。坐标：0..65535(中心0x8000) → -32768..32767；
// 扳机 0..255 → 0..1023。
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

    blePad->setLeftThumb ((int16_t)((int32_t)lx - 0x8000), (int16_t)((int32_t)ly - 0x8000));
    blePad->setRightThumb((int16_t)((int32_t)rx - 0x8000), (int16_t)((int32_t)ry - 0x8000));
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

// 按类型构造 BLE HID 设备并加入 CompositeHID。当前仅 BLE_PAD_XBOX 实现，
// DUALSENSE/NSPRO 预留：选择器对未实现类型已统一返回 XBOX，故此处不会收到。
static XboxGamepadDevice *buildBlePad(uint8_t padType, BLEHostConfiguration &hostCfg,
                                      const char *&devName, const char *mfrName) {
    switch (padType) {
        // 预留：CompositeHID 库补齐 DualSenseDevice/NSProDevice 后在此构造，
        // 对应 VID/PID、HID 描述符、按键映射 bleBtnTable 一并扩展。
        case BLE_PAD_DUALSENSE:
        case BLE_PAD_NSPRO:
        case BLE_PAD_XBOX:
        default: {
            // Xbox Series X：VID=0x045E / PID=0x0B13 / BCD=0x0509，
            // Windows 10/11 原生识别为 Xbox 手柄。
            auto *cfg = new XboxSeriesXControllerDeviceConfiguration();
            hostCfg = cfg->getIdealHostConfiguration();
            cfg->setAutoReport(false);                 // 由我们按 on-change 手动 send
            devName = "Xbox Wireless Controller";      // 对齐真手柄 GAP Device Name
            mfrName = "Microsoft Corporation";
            return new XboxGamepadDevice(cfg);
        }
    }
}

void bleBegin(uint8_t padType) {
    if (!bleStackStarted) {
        bleSetUniqueAddress();   // 必须在 NimBLE init 前改变身份地址
        BLEHostConfiguration hostCfg;
        const char *devName = nullptr;
        const char *mfrName = nullptr;
        blePad = buildBlePad(padType, hostCfg, devName, mfrName);
        // 设备名/厂商名对齐真手柄：Windows 各层（蓝牙设置、HID product、测试工具）
        // 显示的设备名来自 GAP Device Name，名为 "GP2040-CE-BLE" 时会显示成
        // XINPUT IG/Standard Gamepad 等通用节点。
        bleHid = new BleCompositeHID(devName, mfrName, 100);
        bleHid->addDevice(blePad);
        bleHid->begin(hostCfg);
        activeBlePadType = padType;
        bleStackStarted = true;
        bleStackStartMs = millis();
    } else if (NimBLEDevice::isInitialized()) {
        // 再次进入：栈仍在，用同一个传统广播对象恢复广播（含名字/服务/appearance）
        NimBLEDevice::getServer()->getAdvertising()->start();
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

// BLE 任务：连接后按固定 5ms 节拍发送 HID report；同时维护 100ms LINK_STATUS 心跳。
// 等待 onAuthenticationComplete（CCCD 恢复完毕）后再发首帧，避免 notify 静默失败。
void bleTask(void *) {
    bool wasConnected = false;
    bool wasReady = false;
    bool advNamed = false;
    uint32_t connMs = 0;      // 连接时刻
    uint32_t lastParamMs = 0; // 上次请求高速连接参数时刻
    uint32_t lastPollMs = 0;  // 上次间隔轮询时刻
    uint32_t lastLinkStatusMs = 0; // 上次 LINK_STATUS 心跳时刻
    for (;;) {
        uint32_t nowMs = millis();

        // LINK_STATUS 心跳：100ms 一帧；任务常驻，休眠期间也持续上报（连接位为 0）。
        // 本固件 UART TX 仅此一处写者，与 RX 路径（loop 只读）无并发写冲突。
        if (nowMs - lastLinkStatusMs >= LINK_STATUS_HEARTBEAT_MS) {
            sendLinkStatus();
            lastLinkStatusMs = nowMs;
        }

        // 运行中 BLE 设备类型变化：GATT 服务/HID 描述符在 NimBLE init 时一次定型，
        // 栈内热重建不稳定；Pico 切换手柄模式本身会重启，这里同样以自重启收敛。
        // 当前选择器对所有模式均返回 XBOX，实际不会触发；非 Xbox 类型实现后生效。
        if (bleRunning && bleDesired && stBlePadType != activeBlePadType) {
            ESP.restart();
        }
        // 状态收敛：NimBLE 栈的启停一律在本任务(core0)上下文执行。
        if (bleDesired != bleRunning) {
            if (bleDesired) {
                bleBegin(stBlePadType);
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
                } else {
                    // 仅在"已连接→断开"边沿恢复广播。必须以 bleRunning 为前提，
                    // 若此处无条件复活广播，休眠后 ESP32 仍在 BLE 广播形成"幽灵连接"。
                    if (wasConnected && bleRunning && NimBLEDevice::isInitialized()) {
                        NimBLEDevice::getServer()->getAdvertising()->start();
                    }
                }
            }
            wasConnected = conn;

            // 认证/CCCD 恢复就绪检测：onAuthenticationComplete 后 isReady()=true
            bool ready = bleHid->isReady();
            if (ready && !wasReady) {
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
                nowMs = millis();
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

                // 5ms 固定发送：每 5ms 发送一次 HID report，保证稳定回报率
                bleApplyState(btns, dpad, lx, ly, rx, ry, lt, rt);
                blePad->sendGamepadReport();
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
                // 看门狗活动判据在 loop 按"任意字节"刷新，与帧内容/CRC 无关；
                // 这里只分发 CRC 合法帧，其余类型校验后忽略。
                if (rxType == FRAME_TYPE_INPUT) onInputFrame(rxPayload, rxLen);
                else if (rxType == FRAME_TYPE_STATUS) onStatusFrame(rxPayload, rxLen);
                // 其余帧类型（CONFIG/LED/ESP_SAVE/MUTE 等）无菜单/持久化需求，忽略
            }
            rxState = 0;
            break;
    }
}

// ============================================================================
// WS2812 状态灯：仅状态变化时刷新，100ms 节流（慢闪由时间驱动）
//   Pico 离线（蓝牙开关关闭/关机）= 红慢闪(1Hz)
//   BLE 广播中=蓝慢闪(1Hz)
//   BLE 已连接+间隔达标=蓝常亮 / 已连接但间隔未达标(47Hz指纹)=蓝快闪(4Hz)
//   BLE 栈未运行=紫(异常指纹)
// ============================================================================
void updateLed() {
    unsigned long now = millis();
    if (now - lastLedMs < 100) return;
    lastLedMs = now;

    uint8_t r = 0, g = 0, b = 0;
    if (linkSleeping) {
        bool on = (now / 500) % 2 == 0; // Pico 离线：1Hz 红色慢闪
        if (on) r = 48;
    } else if (!bleRunning) {
        // 栈启动收敛期（NimBLE init 数百 ms）不判异常；启动 3s 后仍未运行才显紫
        if (now > 3000) { r = 40; b = 40; }
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

    if (r != ledR || g != ledG || b != ledB) {
        ledR = r; ledG = g; ledB = b;
        ledWrite(r, g, b);
    }
}

// ============================================================================
// setup / loop
// ============================================================================
void setup() {
    // 降频 80MHz 省电：BLE 控制器有独立基带时钟，降频不影响蓝牙。
    setCpuFrequencyMhz(80);

    // 上电先点红，进入 loop 后由状态机接管
    ledWrite(48, 0, 0);
    ledR = 48;

    LINK_SERIAL.begin(UART_BAUD, SERIAL_8N1, ESP_RX, ESP_TX);
    // 开机即请求启动 BLE；UART 看门狗按 Pico 在线状态收敛（静默 1s 后停）
    bleDesired = true;

    xTaskCreatePinnedToCore(bleTask, "ble", 8192, NULL, 1, NULL, 0);     // BLE 任务常驻核0

    // LED 独立任务最后创建：pin 核1，优先级 2 高于 loopTask。
    // loop 在 UART 处理路径可能被饿死/阻塞，updateLed 不放 loop 里。
    xTaskCreatePinnedToCore(ledTask, "led", 2560, NULL, 2, NULL, 1);
}

// 独立 LED 任务：优先级 2（高于 loop/ble 的 1），即便 loopTask 被 UART
// 处理路径饿死或阻塞，本任务仍能被调度；neopixelWrite 由此任务独占，杜绝与 loop
// 并发访问 RMT。节拍 20ms，updateLed 内部另有 100ms 节流。
void ledTask(void *) {
    for (;;) {
        updateLed();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void loop() {
    // 看门狗活动判据：drain 到任意字节即刷新（每批一次 millis()）。
    // 不要求 CRC 合法——误码/丢包期间线路仍有活动，说明 Pico 仍在蓝牙模式，
    // 只有线路完全静默（Pico 切走/关机）才允许休眠。
    bool active = false;
    while (LINK_SERIAL.available()) {
        handleRxByte((uint8_t)LINK_SERIAL.read());
        active = true;
    }
    uint32_t nowMs = millis();
    if (active) lastUartByteMs = nowMs;

    bool uartAlive = (lastUartByteMs != 0) &&
                     (nowMs - lastUartByteMs < UART_LINK_LOST_MS);
    if (!linkSleeping && !uartAlive && nowMs >= UART_BOOT_GRACE_MS) {
        // Pico 静默：停 BLE 省电（实际启停由 bleTask 在 core0 执行）
        linkSleeping = true;
        bleDesired = false;
    } else if (linkSleeping && uartAlive) {
        // Pico 恢复：重新请求启动 BLE
        linkSleeping = false;
        bleDesired = true;
    }
}
