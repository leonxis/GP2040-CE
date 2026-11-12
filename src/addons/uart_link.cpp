#include "addons/uart_link.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "drivermanager.h"
#include "pico/time.h"
#include "pico/platform.h"

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"

#include "uart_link.pio.h"

// 跨核标志位：Core1 的 DisplayAddon 设置，true 时处于屏幕操作状态（miniled 菜单）。
extern volatile bool g_screenOperationActive;

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
static uint16_t __not_in_flash_func(crc16_update)(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

// available() 由网页开关驱动：扩展蓝牙 / 扩展2.4G无线 任一开启即启用该插件。
// 具体走 BLE 还是 nRF24 由 STATUS 帧的 radioMode 字段告知 ESP32 侧切换。
bool UARTLinkAddon::available() {
    const GamepadOptions& options = Storage::getInstance().getGamepadOptions();
    return options.bluetoothLinkEnabled || options.wireless24gLinkEnabled;
}

void UARTLinkAddon::setup() {
    if (initialized) return;

    // 重置状态
    lastInputSentUs = 0;
    lastStatusSentUs = 0;
    lastTxStartUs = 0;
    lastInputMode = 0xFF;
    lastRadioMode = 0xFF;
    lastLinkStatusUs = 0;
    Storage::getInstance().setUartLinkStatus(false, false);
    rxState = 0;
    rxType = 0;
    rxLen = 0;
    rxIdx = 0;
    rxCrcCalc = 0;
    rxCrcLo = 0;
    dmaChanTx = -1;

    // --- PIO0 SM0/SM1 覆盖：先 disable 再 init ---
    // USB 验证器（blockUSB0）开启时才会调用 tuh_init 加载 pico_usb；
    // uart_link 启用时 blockUSB0=false，PIO0 SM0/SM1 完全空闲。
    pio_sm_set_enabled(UART_LINK_PIO, UART_LINK_SM_TX, false);
    pio_sm_set_enabled(UART_LINK_PIO, UART_LINK_SM_RX, false);

    // 加载两个 PIO 程序到 PIO0 instruction memory（失败返回 -1，说明指令空间不足）
    int txProgramOffset = pio_add_program(UART_LINK_PIO, &uart_tx_program);
    int rxProgramOffset = pio_add_program(UART_LINK_PIO, &uart_rx_program);
    if (txProgramOffset < 0 || rxProgramOffset < 0) {
        // 不能用无效 offset 配置 SM，直接保持未初始化，链路不启用
        return;
    }
    txOffset = (uint)txProgramOffset;
    rxOffset = (uint)rxProgramOffset;

    const float clkdiv = (float)clock_get_hz(clk_sys) / (8.0f * (float)UART_LINK_BAUD);

    // --- GPIO 复用 ---
    // 必须把引脚功能选择切到 PIO，否则 PIO 的 out/in 信号不会接到 GPIO 上
    pio_gpio_init(UART_LINK_PIO, UART_LINK_TX_PIN);
    pio_gpio_init(UART_LINK_PIO, UART_LINK_RX_PIN);
    gpio_set_pulls(UART_LINK_RX_PIN, true, false);

    // --- TX SM：SM0 ---
    // 用 pioasm 生成的默认配置（含 .side_set 1 opt 对应的 sideset 设置）
    pio_sm_config tx_cfg = uart_tx_program_get_default_config(txOffset);
    sm_config_set_out_pins(&tx_cfg, UART_LINK_TX_PIN, 1);
    sm_config_set_set_pins(&tx_cfg, UART_LINK_TX_PIN, 1);   // start/stop bit 的 set pins 作用于 TX
    sm_config_set_sideset_pins(&tx_cfg, UART_LINK_TX_PIN);  // side_set 驱动同一 pin
    sm_config_set_clkdiv(&tx_cfg, clkdiv);
    pio_sm_init(UART_LINK_PIO, UART_LINK_SM_TX, txOffset, &tx_cfg);
    // 先预置 TX 引脚输出高电平，再设为输出方向，避免 SM 使能前引脚被拉低
    // 产生假起始位（参考 pico-examples uart_tx_program_init 的做法）
    pio_sm_set_pins_with_mask64(UART_LINK_PIO, UART_LINK_SM_TX,
                                1ull << UART_LINK_TX_PIN, 1ull << UART_LINK_TX_PIN);
    // TX 引脚方向设为输出（由 PIO 控制，不能用 gpio_set_dir）
    pio_sm_set_consecutive_pindirs(UART_LINK_PIO, UART_LINK_SM_TX,
                                   UART_LINK_TX_PIN, 1, true);

    // --- RX SM：SM1 ---
    // 显式设为输入方向（pio_gpio_init 不改方向，缺省依赖复位值不可靠）
    pio_sm_set_consecutive_pindirs(UART_LINK_PIO, UART_LINK_SM_RX,
                                   UART_LINK_RX_PIN, 1, false);
    // in_shift_right=true：新采样进 ISR 最高位，8 次 LSB-first 采样后高 8 位 = 原始顺序
    // autopush=true, 阈值 8：8 个数据位由硬件自动入 FIFO，PIO 程序内不再显式 push
    pio_sm_config rx_cfg = uart_rx_program_get_default_config(rxOffset);
    sm_config_set_in_pins(&rx_cfg, UART_LINK_RX_PIN);
    sm_config_set_jmp_pin(&rx_cfg, UART_LINK_RX_PIN);
    sm_config_set_clkdiv(&rx_cfg, clkdiv);
    sm_config_set_in_shift(&rx_cfg, true, true, 8);  // 右移 + 自动 push，阈值 8 位
    // RX FIFO join 后 8 深：ESP32 7 字节 LINK_STATUS 背靠背约 76us 发完，
    // 默认 4 深会在主循环排空前溢出丢帧；8 深恰好容纳整帧突发
    sm_config_set_fifo_join(&rx_cfg, PIO_FIFO_JOIN_RX);
    pio_sm_init(UART_LINK_PIO, UART_LINK_SM_RX, rxOffset, &rx_cfg);

    // --- DMA ---
    dmaChanTx = dma_claim_unused_channel(false);
    if (dmaChanTx < 0) {
        // 没有空闲 DMA 通道：不启用链路，避免启动卡死
        return;
    }
    {
        dma_channel_config dcfg = dma_channel_get_default_config(dmaChanTx);
        channel_config_set_transfer_data_size(&dcfg, DMA_SIZE_32);
        channel_config_set_read_increment(&dcfg, true);
        channel_config_set_write_increment(&dcfg, false);
        channel_config_set_dreq(&dcfg,
            pio_get_dreq(UART_LINK_PIO, UART_LINK_SM_TX, true));
        // clear 先跑一次，后续走 dma_channel_transfer_from_buffer_now
        dma_channel_configure(dmaChanTx, &dcfg,
            &UART_LINK_PIO->txf[UART_LINK_SM_TX],   // write addr
            txBuffer,                                // read addr
            0,                                       // transfer count (先 0)
            false);                                  // don't start yet
    }

    // 开 SM
    pio_sm_set_enabled(UART_LINK_PIO, UART_LINK_SM_TX, true);
    pio_sm_set_enabled(UART_LINK_PIO, UART_LINK_SM_RX, true);

    initialized = true;
}

// 把字节帧打包成 DMA 缓冲（每个字节占一个 uint32_t 的低 8 位）
static void __not_in_flash_func(packFrameToBuffer)(const uint8_t* frame, int len,
                                                    uint32_t* buf) {
    for (int i = 0; i < len; i++) {
        buf[i] = (uint32_t)frame[i];
    }
}

bool __not_in_flash_func(UARTLinkAddon::sendInputFrame)(uint16_t buttons, uint8_t dpad,
                                   uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry,
                                   uint8_t lt, uint8_t rt) {
    if (!initialized || dmaChanTx < 0) return false;
    // DMA 正在发上一帧则跳过（INPUT 2ms 间隔足够 206μs 完成）
    if (dma_channel_is_busy(dmaChanTx)) return false;

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

    packFrameToBuffer(frame, 19, txBuffer);
    dma_channel_transfer_from_buffer_now(dmaChanTx, txBuffer, 19);
    lastTxStartUs = to_us_since_boot(get_absolute_time());
    return true;
}

bool UARTLinkAddon::sendStatusFrame(uint8_t inputMode, uint8_t radioMode) {
    if (!initialized || dmaChanTx < 0) return false;
    if (dma_channel_is_busy(dmaChanTx)) return false;

    uint8_t frame[10];
    frame[0] = LINK_FRAME_MAGIC;
    frame[1] = LINK_FRAME_VERSION;
    frame[2] = LINK_FRAME_TYPE_STATUS;
    frame[3] = 4;
    frame[4] = 0;
    frame[5] = 0;
    frame[6] = inputMode;
    frame[7] = radioMode;
    uint16_t crc = 0xFFFF;
    for (int i = 1; i <= 7; i++) crc = crc16_update(crc, frame[i]);
    frame[8] = (uint8_t)(crc & 0xFF);
    frame[9] = (uint8_t)(crc >> 8);

    packFrameToBuffer(frame, 10, txBuffer);
    dma_channel_transfer_from_buffer_now(dmaChanTx, txBuffer, 10);
    lastTxStartUs = to_us_since_boot(get_absolute_time());
    return true;
}

void UARTLinkAddon::handleRxByte(uint8_t b) {
    // 保留帧同步与 CRC 校验；ESP32→Pico 帧中仅 LINK_STATUS 分发（未配对灯效），
    // 其余类型（ACK/CONFIG/LED/ESP_SAVE/ESP_LOAD_REQ/MUTE）校验完成后丢弃。
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
            if (b == (uint8_t)(rxCrcCalc >> 8) &&
                rxCrcLo == (uint8_t)(rxCrcCalc & 0xFF) &&
                rxType == LINK_FRAME_TYPE_LINK_STATUS && rxLen >= 1) {
                handleLinkStatus();
            }
            rxState = 0;
            break;
    }
}

void UARTLinkAddon::handleLinkStatus() {
    const bool bleConnected = rxPayload[0] != 0;
    lastLinkStatusUs = to_us_since_boot(get_absolute_time());
    Storage::getInstance().setUartLinkStatus(true, bleConnected);
}

void __not_in_flash_func(UARTLinkAddon::process)() {
    if (!initialized) return;
    // PIO RX FIFO join 后 8 深，可容纳 ESP32 一整帧 7 字节背靠背突发；
    // 主循环每轮排空，LINK_STATUS 心跳 100ms 远大于主循环周期，不会积压。
    // 进循环前已确认非空，pio_sm_get 不阻塞。
    while (!pio_sm_is_rx_fifo_empty(UART_LINK_PIO, UART_LINK_SM_RX)) {
        uint8_t b = (uint8_t)((pio_sm_get(UART_LINK_PIO, UART_LINK_SM_RX) >> 24) & 0xFF);
        handleRxByte(b);
    }
}

void __not_in_flash_func(UARTLinkAddon::postprocess)(bool sent) {
    (void)sent;
    if (!initialized) return;

    uint32_t now = to_us_since_boot(get_absolute_time());

    // TX 看门狗：单帧最长约 206us，超过 20ms 仍 busy 说明 SM 停摆或 DREQ
    // 不再触发，abort 通道让下一次发送重新装载，避免永久 busy 静默死链。
    if (dmaChanTx >= 0 && dma_channel_is_busy(dmaChanTx) &&
        now - lastTxStartUs >= UART_TX_STUCK_TIMEOUT_US) {
        dma_channel_abort(dmaChanTx);
    }

    // ESP32 离线检测：LINK_STATUS 心跳（100ms）静默超过 500ms 判离线。
    if (lastLinkStatusUs != 0 &&
        now - lastLinkStatusUs >= UART_LINK_STATUS_TIMEOUT_US) {
        lastLinkStatusUs = 0;
        Storage::getInstance().setUartLinkStatus(false, false);
    }

    Gamepad* g = Storage::getInstance().GetProcessedGamepad();
    if (g == nullptr) return;

    uint16_t buttons;
    uint8_t dpad;
    uint16_t lx, ly, rx, ry;
    uint8_t lt, rt;
    if (g_screenOperationActive) {
        buttons = 0;
        dpad = 0;
        lx = ly = rx = ry =
            DriverManager::getInstance().getDriver()->GetJoystickMidValue();
        lt = rt = 0;
    } else {
        buttons = (uint16_t)(g->state.buttons & 0xFFFF);
        dpad = (uint8_t)g->state.dpad;
        lx = (uint16_t)g->state.lx;
        ly = (uint16_t)g->state.ly;
        rx = (uint16_t)g->state.rx;
        ry = (uint16_t)g->state.ry;
        lt = (uint8_t)g->state.lt;
        rt = (uint8_t)g->state.rt;
    }

    if (now - lastInputSentUs >= UART_INPUT_INTERVAL_US) {
        if (sendInputFrame(buttons, dpad, lx, ly, rx, ry, lt, rt))
            lastInputSentUs = now;
    }

    const GamepadOptions& options = Storage::getInstance().getGamepadOptions();
    uint8_t inputMode = (uint8_t)options.inputMode;
    // radioMode: 0=BLE（扩展蓝牙），1=nRF24（扩展2.4G无线）；两开关互斥已归一化
    uint8_t radioMode = options.wireless24gLinkEnabled ? 1 : 0;
    if (inputMode != lastInputMode || radioMode != lastRadioMode ||
        now - lastStatusSentUs >= UART_STATUS_HEARTBEAT_US) {
        if (sendStatusFrame(inputMode, radioMode)) {
            lastInputMode = inputMode;
            lastRadioMode = radioMode;
            lastStatusSentUs = now;
        }
    }
}
