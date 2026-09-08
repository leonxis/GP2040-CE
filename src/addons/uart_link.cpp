#include "addons/uart_link.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "pico/time.h"

#include "hardware/uart.h"
#include "hardware/gpio.h"

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
static uint16_t crc16_update(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

// available() 由存储字段 wirelessLinkEnabled 驱动（网页/miniled 菜单"无线连接"开关）。
// config_utils 初始化时已通过 INIT_UNSET_PROPERTY 写入板级默认值并置 has_ 标志，
// 此处直接读取值即可。
bool UARTLinkAddon::available() {
    if (!UART_LINK_ENABLED) return false;
    return Storage::getInstance().getGamepadOptions().wirelessLinkEnabled;
}

void UARTLinkAddon::setup() {
    if (!initialized) {
        // UART1: GPIO8(TX)/GPIO9(RX)，与 USB0 D+/D- 复用（互斥），921600 波特率
        uart_init(uart1, UART_LINK_BAUD);
        gpio_set_function(UART_LINK_TX_PIN, GPIO_FUNC_UART);
        gpio_set_function(UART_LINK_RX_PIN, GPIO_FUNC_UART);
        gpio_set_pulls(UART_LINK_RX_PIN, true, false);

        lastSent = 0;
        lastButtons = 0;
        lastDpad = 0;
        lastLx = 0; lastLy = 0; lastRx = 0; lastRy = 0;
        lastLt = 0; lastRt = 0;
        lastStatusSent = 0;
        lastInputMode = 0xFF;
        rxState = 0;
        rxType = 0;
        rxLen = 0;
        rxIdx = 0;
        rxCrcCalc = 0;
        rxCrcLo = 0;
        initialized = true;
    }
}

void UARTLinkAddon::sendInputFrame(uint16_t buttons, uint8_t dpad,
                                   uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry,
                                   uint8_t lt, uint8_t rt) {
    uint8_t frame[19];
    frame[0] = LINK_FRAME_MAGIC;
    frame[1] = LINK_FRAME_VERSION;
    frame[2] = LINK_FRAME_TYPE_INPUT;
    frame[3] = 13; // buttons(2) dpad(1) lx(2) ly(2) rx(2) ry(2) lt(1) rt(1)
    frame[4] = (uint8_t)(buttons & 0xFF);
    frame[5] = (uint8_t)(buttons >> 8);
    frame[6] = dpad;
    frame[7] = (uint8_t)(lx & 0xFF);
    frame[8] = (uint8_t)(lx >> 8);
    frame[9] = (uint8_t)(ly & 0xFF);
    frame[10] = (uint8_t)(ly >> 8);
    frame[11] = (uint8_t)(rx & 0xFF);
    frame[12] = (uint8_t)(rx >> 8);
    frame[13] = (uint8_t)(ry & 0xFF);
    frame[14] = (uint8_t)(ry >> 8);
    frame[15] = lt;
    frame[16] = rt;
    uint16_t crc = 0xFFFF;
    for (int i = 1; i <= 16; i++) crc = crc16_update(crc, frame[i]);
    frame[17] = (uint8_t)(crc & 0xFF);
    frame[18] = (uint8_t)(crc >> 8);
    uart_write_blocking(uart1, frame, sizeof(frame));
}

void UARTLinkAddon::sendStatusFrame(uint8_t inputMode) {
    // 仅 inputMode 真实——它是 nRF 包 pkt[0] 的唯一来源；
    // 其余字段全部固定默认值（不读取设置），ESP32 仅显示用。
    // LED/电池: 灯效用 GP2040-CE 自有配置，ESP32 显示 USB 供电满电。
    const uint8_t frame[24] = {
        LINK_FRAME_MAGIC,
        LINK_FRAME_VERSION,
        LINK_FRAME_TYPE_STATUS,
        18,             // gamepad + LED + battery status
        0,              // socdMode: 默认(UPRIGHT)
        0,              // dpadMode: 默认(DIGITAL)
        inputMode,      // inputMode: 真实值
        0,              // flags: invertX/Y/fourWay 全 0
        5,              // debounce: 默认
        0,              // animIndex: 默认
        0xFF,           // brightness: 最大
        0,              // staticColorIndex: 默认
        0, 0,           // chaseCycle: 0
        0, 0,           // rainbowCycle: 0
        0,              // ledFlags: 0
        0, 0,           // flowCycle: 0
        (uint8_t)(4200 & 0xFF), (uint8_t)(4200 >> 8),  // battMv: 4200 满电
        0x03,           // battFlags: valid + USB 供电
        0, 0            // CRC 占位
    };
    uint8_t out[24];
    memcpy(out, frame, sizeof(out));
    uint16_t crc = 0xFFFF;
    for (int i = 1; i <= 21; i++) crc = crc16_update(crc, out[i]);
    out[22] = (uint8_t)(crc & 0xFF);
    out[23] = (uint8_t)(crc >> 8);
    uart_write_blocking(uart1, out, sizeof(out));
}

void UARTLinkAddon::handleRxByte(uint8_t b) {
    // 保留帧同步与 CRC 校验；所有 ESP32→Pico 帧类型（ACK/CONFIG/LED/
    // ESP_SAVE/ESP_LOAD_REQ/MUTE）一律不处理，校验完成后丢弃。
    switch (rxState) {
        case 0:
            if (b == LINK_FRAME_MAGIC) rxState = 1;
            break;
        case 1:
            rxCrcCalc = 0xFFFF;
            rxCrcCalc = crc16_update(rxCrcCalc, b);
            rxState = (b == LINK_FRAME_VERSION) ? 2 : 0;
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
            // CRC 校验通过与否都不分发——发射端无任何接收处理需求
            rxState = 0;
            break;
    }
}

void UARTLinkAddon::process() {
    if (!initialized) return;
    while (uart_is_readable(uart1)) {
        handleRxByte((uint8_t)uart_getc(uart1));
    }
}

void UARTLinkAddon::postprocess(bool sent) {
    (void)sent;
    if (!initialized) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    Gamepad* g = Storage::getInstance().GetProcessedGamepad();
    if (g == nullptr) return;

    uint16_t buttons = (uint16_t)(g->state.buttons & 0xFFFF);
    uint8_t dpad = (uint8_t)g->state.dpad;
    uint16_t lx = (uint16_t)g->state.lx, ly = (uint16_t)g->state.ly;
    uint16_t rx = (uint16_t)g->state.rx, ry = (uint16_t)g->state.ry;
    uint8_t lt = (uint8_t)g->state.lt, rt = (uint8_t)g->state.rt;
    if (buttons != lastButtons || dpad != lastDpad ||
        lx != lastLx || ly != lastLy || rx != lastRx || ry != lastRy ||
        lt != lastLt || rt != lastRt || now - lastSent >= 50) {
        sendInputFrame(buttons, dpad, lx, ly, rx, ry, lt, rt);
        lastButtons = buttons;
        lastDpad = dpad;
        lastLx = lx; lastLy = ly; lastRx = rx; lastRy = ry;
        lastLt = lt; lastRt = rt;
        lastSent = now;
    }

    // inputMode 变化立即上报 + 1s 心跳，保证 nRF 包 pkt[0] 跟随真实输入模式
    const GamepadOptions& options = Storage::getInstance().getGamepadOptions();
    uint8_t inputMode = (uint8_t)options.inputMode;
    if (inputMode != lastInputMode || now - lastStatusSent >= 1000) {
        sendStatusFrame(inputMode);
        lastInputMode = inputMode;
        lastStatusSent = now;
    }
}
