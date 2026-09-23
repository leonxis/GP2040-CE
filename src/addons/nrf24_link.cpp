#include "addons/nrf24_link.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "peripheralmanager.h"

#include "hardware/gpio.h"
#include "pico/time.h"

// available() 由 GamepadOptions.nrf24LinkEnabled 单独驱动（与 uart_link 互斥，三选一）
bool NRF24LinkAddon::available() {
    if (!NRF24_LINK_ENABLED) return false;
    const GamepadOptions& o = Storage::getInstance().getGamepadOptions();
    return o.nrf24LinkEnabled;
}

void NRF24LinkAddon::setup() {
    if (initialized) return;

    // 1. 获取 SPI0 实例（PeripheralManager 在 config_utils 中已按 SPI0_ENABLED 完成默认初始化）
    PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(NRF24_HW_SPI_BLOCK);
    if (!spi || !spi->configured) return;
    spi_ = spi;
    spiProfile_ = spi_->makeBaudrateProfile(NRF24_SPI_HZ);
    if (!spiProfile_.valid()) return;
    spi_->beginTransaction(spiProfile_, SPI_MSB_FIRST, SPI_MODE0);

    csPin_ = NRF24_HW_CS_PIN;
    cePin_ = NRF24_HW_CE_PIN;

    // 2. CE 引脚为 nRF24 专用（非 SPI CS），手动初始化
    gpio_init(cePin_);
    gpio_set_dir(cePin_, GPIO_OUT);
    gpio_put(cePin_, 0);
    sleep_ms(10);  // 等待 nRF24 上电稳定

    // 3. nRF24 寄存器配置（与 ESP32 端 nrf24.h 完全一致以确保互通）
    writeReg(NRF24_REG_CONFIG, 0x00);            // power down
    writeReg(NRF24_REG_EN_AA, 0x01);             // EN_AA: 仅 pipe0（auto-ack）
    writeReg(NRF24_REG_EN_RXADDR, 0x01);         // EN_RXADDR: 仅 pipe0
    writeReg(NRF24_REG_SETUP_AW, 0x03);           // SETUP_AW: 5-byte addresses
    writeReg(NRF24_REG_SETUP_RETR, 0x13);         // SETUP_RETR: 250us, 3 retries
    writeReg(NRF24_REG_RF_CH, NRF24_CHANNEL);     // RF_CH
    writeReg(NRF24_REG_RF_SETUP, 0x0E);           // RF_SETUP: 2Mbps, 0dBm
    static const uint8_t addr[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
    writeRegBuf(NRF24_REG_TX_ADDR, addr, 5);      // TX_ADDR
    writeRegBuf(NRF24_REG_RX_ADDR_P0, addr, 5);   // RX_ADDR_P0 (auto-ack pipe)
    writeReg(NRF24_REG_RX_PW_P0, NRF24_PAYLOAD);  // RX_PW_P0
    writeReg(NRF24_REG_STATUS, 0x70);             // clear STATUS
    writeReg(NRF24_REG_CONFIG, 0x0E);             // PWR_UP + 2字节CRC
    sleep_ms(2);  // PowerUp + OscSettling 时间

    // 4. 初始化节流缓存
    lastSentUs = 0;
    lastButtons = 0;
    lastDpad = 0;
    lastLx = 0; lastLy = 0; lastRx = 0; lastRy = 0;
    lastLt = 0; lastRt = 0;
    lastInputMode = 0xFF;

    // 异步 TX 状态复位（reinit 路径同样经过此处）
    txPending = false;
    txDirty = false;
    txStartUs = 0;

    // 上电/重初始化默认接收端离线：环境光立即红闪，直到 ACK 去抖通过
    linkUp = false;
    linkAckStreak = 0;
    lastAckUs = 0;
    Storage::getInstance().setNrf24LinkUp(false);

    initialized = true;
}

void NRF24LinkAddon::writeReg(uint8_t reg, uint8_t val) {
    if (!spi_) return;
    spi_->select(csPin_);
    spi_->transfer(static_cast<uint8_t>(0x20 | reg));
    spi_->transfer(val);
    spi_->deselect();
}

void NRF24LinkAddon::writeRegBuf(uint8_t reg, const uint8_t *data, uint8_t len) {
    if (!spi_) return;
    spi_->select(csPin_);
    spi_->transfer(static_cast<uint8_t>(0x20 | reg));
    for (uint8_t i = 0; i < len; i++) spi_->transfer(data[i]);
    spi_->deselect();
}

uint8_t NRF24LinkAddon::readReg(uint8_t reg) {
    if (!spi_) return 0;
    spi_->select(csPin_);
    spi_->transfer(reg);
    uint8_t v = spi_->transfer(0);
    spi_->deselect();
    return v;
}

void NRF24LinkAddon::flushTx() {
    if (!spi_) return;
    spi_->select(csPin_);
    spi_->transfer(NRF24_CMD_FLUSH_TX);
    spi_->deselect();
}

void NRF24LinkAddon::cePulse(uint32_t us) {
    gpio_put(cePin_, 1);
    busy_wait_us(us);
    gpio_put(cePin_, 0);
}

void NRF24LinkAddon::submitPacket(const uint8_t *data) {
    // 写 TX FIFO + CE 脉冲即返回，ACK 结果由后续帧 pollTxComplete 判定
    writeReg(NRF24_REG_STATUS, 0x70);  // clear STATUS
    spi_->select(csPin_);
    spi_->transfer(NRF24_CMD_W_TX_PAYLOAD);
    for (int i = 0; i < NRF24_PAYLOAD; i++) spi_->transfer(data[i]);
    spi_->deselect();
    txStartUs = time_us_32();
    cePulse(NRF24_CE_PULSE_US);  // 触发发射
    txPending = true;
}

int8_t NRF24LinkAddon::pollTxComplete() {
    // 软件超时优先（纯时间比较，无额外 SPI 开销）：
    // SETUP_RETR(0x13) 下硬件事务最长 ~2ms，超时判芯片异常并恢复
    if (static_cast<int32_t>(time_us_32() - txStartUs) >=
            NRF24_TX_POLL_TIMEOUT_US) {
        writeReg(NRF24_REG_STATUS, 0x70);
        flushTx();
        return 0;
    }
    // 每次调用仅读一次 STATUS，不做循环等待
    uint8_t st = readReg(NRF24_REG_STATUS);
    if (st & NRF24_STATUS_TX_DS) {
        writeReg(NRF24_REG_STATUS, NRF24_STATUS_TX_DS);
        return 1;  // TX_DS: ACK 收到
    }
    if (st & NRF24_STATUS_MAX_RT) {
        writeReg(NRF24_REG_STATUS, NRF24_STATUS_MAX_RT);
        flushTx();
        return 0;  // MAX_RT: 重试失败
    }
    return -1;  // 仍在进行
}

void NRF24LinkAddon::submitInputFrame(uint16_t buttons, uint8_t dpad,
                                      uint16_t lx, uint16_t ly,
                                      uint16_t rx, uint16_t ry,
                                      uint8_t lt, uint8_t rt, uint8_t inputMode) {
    // 15 字节 payload：复用 uart_link 帧的 13 字节游戏pad状态 +
    // inputMode（接收端可识别源模式）+ reserved
    uint8_t payload[NRF24_PAYLOAD];
    payload[0]  = static_cast<uint8_t>(buttons & 0xFF);
    payload[1]  = static_cast<uint8_t>(buttons >> 8);
    payload[2]  = dpad;
    payload[3]  = static_cast<uint8_t>(lx & 0xFF);
    payload[4]  = static_cast<uint8_t>(lx >> 8);
    payload[5]  = static_cast<uint8_t>(ly & 0xFF);
    payload[6]  = static_cast<uint8_t>(ly >> 8);
    payload[7]  = static_cast<uint8_t>(rx & 0xFF);
    payload[8]  = static_cast<uint8_t>(rx >> 8);
    payload[9]  = static_cast<uint8_t>(ry & 0xFF);
    payload[10] = static_cast<uint8_t>(ry >> 8);
    payload[11] = lt;
    payload[12] = rt;
    payload[13] = inputMode;
    payload[14] = 0;  // reserved
    submitPacket(payload);
}

void NRF24LinkAddon::process() {
    // 发射端单向输出，无接收处理需求（与 uart_link 一致策略）
}

void NRF24LinkAddon::postprocess(bool sent) {
    (void)sent;
    if (!initialized || !spi_) return;

    // 微秒计时：与 uart_link 一致，整数毫秒在高频循环下会产生同毫秒双跳过
    uint32_t now = time_us_32();

    // 离线超时检测：linkUp 后 ACK 静默超过 1s（≈20 个心跳）判接收端离线。
    // lastAckUs==0（开机从未收到 ACK）时 linkUp 本就为 false，无需处理。
    if (linkUp && (now - lastAckUs) >= NRF24_LINK_DOWN_TIMEOUT_US) {
        linkUp = false;
        linkAckStreak = 0;
        Storage::getInstance().setNrf24LinkUp(false);
    }

    Gamepad* g = Storage::getInstance().GetProcessedGamepad();
    if (g == nullptr) return;

    // 始终读取【最新】绝对状态：payload 是状态快照而非事件流，
    // 被跳过的中间快照语义等同无线丢包，无需排队补寄
    uint16_t buttons = static_cast<uint16_t>(g->state.buttons & 0xFFFF);
    uint8_t  dpad    = static_cast<uint8_t>(g->state.dpad);
    uint16_t lx = static_cast<uint16_t>(g->state.lx);
    uint16_t ly = static_cast<uint16_t>(g->state.ly);
    uint16_t rx = static_cast<uint16_t>(g->state.rx);
    uint16_t ry = static_cast<uint16_t>(g->state.ry);
    uint8_t  lt = static_cast<uint8_t>(g->state.lt);
    uint8_t  rt = static_cast<uint8_t>(g->state.rt);

    // ===== 阶段 1：在飞事务只做一次 STATUS 轮询 =====
    if (txPending) {
        const int8_t txResult = pollTxComplete();
        if (txResult < 0) {
            // 芯片仍忙：不提交新事务；状态与在飞包不同则置 dirty，
            // 数据不缓存——完成时直接从 Storage 读最新值
            if (buttons != lastButtons || dpad != lastDpad ||
                lx != lastLx || ly != lastLy ||
                rx != lastRx || ry != lastRy ||
                lt != lastLt || rt != lastRt) {
                txDirty = true;
            }
            return;
        }

        // 事务已结束（1=ACK，0=失败），本次调用内继续决定是否提交新包
        txPending = false;
        now = time_us_32();
        // 在线去抖：连续 N 次 ACK 才发布在线（抗上电/干扰偶发 ACK）；
        // 在线后的偶发丢包不翻转标志，由上方 1s 超时统一判离线。
        // ACK 结果比同步模式晚一帧获知，不影响去抖语义。
        if (txResult == 1) {
            lastAckUs = now;
            if (!linkUp && ++linkAckStreak >= NRF24_LINK_UP_STREAK) {
                linkUp = true;
                Storage::getInstance().setNrf24LinkUp(true);
            }
        }
    }

    // ===== 阶段 2：芯片空闲，dirty 优先，否则走混合节流 =====
    //  - dirty（在飞期间状态已变化）：无视节流立即发，保证忙期间输入
    //    不被 900us/50ms 窗口额外拖延；
    //  - 数字键(buttons/dpad)变化立即发，零额外延迟；
    //  - 模拟量连续变化时限 900us（门控循环 0.95~1.04ms 每轮必满足）；
    //  - 50ms 心跳保底同步/抗丢帧。
    bool digitalChanged = (buttons != lastButtons || dpad != lastDpad);
    bool analogChanged  = (lx != lastLx || ly != lastLy ||
                           rx != lastRx || ry != lastRy ||
                           lt != lastLt || rt != lastRt);
    if (txDirty ||
        digitalChanged ||
        (analogChanged && (now - lastSentUs) >= NRF24_INPUT_ANALOG_MIN_US) ||
        (now - lastSentUs) >= NRF24_INPUT_HEARTBEAT_US) {
        const GamepadOptions& options = Storage::getInstance().getGamepadOptions();
        uint8_t inputMode = static_cast<uint8_t>(options.inputMode);
        submitInputFrame(buttons, dpad, lx, ly, rx, ry, lt, rt, inputMode);

        lastButtons = buttons;
        lastDpad = dpad;
        lastLx = lx; lastLy = ly; lastRx = rx; lastRy = ry;
        lastLt = lt; lastRt = rt;
        lastInputMode = inputMode;
        lastSentUs = now;
        txDirty = false;
    }
}

void NRF24LinkAddon::reinit() {
    initialized = false;
    setup();
}
