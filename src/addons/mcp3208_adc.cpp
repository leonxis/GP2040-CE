#include "addons/mcp3208_adc.h"
#include "peripheralmanager.h"

#include "pico/time.h"

static const uint8_t MCP3208_CHANNELS[MCP3208_READ_CHANNELS] = {0, 1, 6, 7};
static const uint8_t MCP3208_TX_COMMANDS[MCP3208_READ_CHANNELS][3] = {
    {0x06, 0x00, 0x00}, // CH0
    {0x06, 0x40, 0x00}, // CH1
    {0x07, 0x80, 0x00}, // CH6
    {0x07, 0xC0, 0x00}, // CH7
};

static inline bool gateDeadlineReached(
    const GateLateAnalogSampleRequest& request,
    uint32_t nowUs
) {
    return request.enforceDeadline &&
        static_cast<int32_t>(nowUs - request.deadlineUs) >= 0;
}

bool MCP3208ADCAddon::available() {
    // Always enabled: no persisted user toggle. Only activates when the
    // fixed SPI peripheral is provided by the board configuration.
    return PeripheralManager::getInstance().isSPIEnabled(MCP3208_HW_SPI_BLOCK);
}

// Static instance for webconfig to read raw stick values (no AddonManager dependency)
MCP3208ADCAddon* MCP3208ADCAddon::s_instance = nullptr;

bool MCP3208ADCAddon::getRawStickForWebConfig(
    uint8_t stickNum,
    uint32_t& x,
    uint32_t& y,
    uint32_t& adcMax
) {
    if (s_instance == nullptr || !s_instance->spiOk_ || stickNum >= MCP3208_STICK_COUNT) {
        return false;
    }
    adcMax = MCP3208_ADC_MAX;
    // Web calibration canvas only needs stick channels.
    (void)s_instance->sampleStickSnapshot();
    const uint8_t snapshotIndex =
        s_instance->publishedStickSnapshot_.load(
            std::memory_order_acquire);
    const StickSnapshot& snapshot =
        s_instance->stickSnapshots_[snapshotIndex];
    x = snapshot.x[stickNum];
    y = snapshot.y[stickNum];
    return true;
}

bool MCP3208ADCAddon::getRawStickForProcessor(
    uint8_t stickNum,
    uint16_t& x,
    uint16_t& y,
    uint16_t& xCenter,
    uint16_t& yCenter,
    bool& xValid,
    bool& yValid,
    uint16_t& adcMax
) {
    if (s_instance == nullptr || !s_instance->spiOk_ || stickNum >= MCP3208_STICK_COUNT) {
        return false;
    }
    const uint8_t snapshotIndex =
        s_instance->publishedStickSnapshot_.load(
            std::memory_order_acquire);
    const StickSnapshot& snapshot =
        s_instance->stickSnapshots_[snapshotIndex];
    x = snapshot.x[stickNum];
    y = snapshot.y[stickNum];
    xCenter = static_cast<uint16_t>(MCP3208_ADC_MAX_HALF);
    yCenter = static_cast<uint16_t>(MCP3208_ADC_MAX_HALF);
    xValid = true;
    yValid = true;
    adcMax = MCP3208_ADC_MAX;
    return true;
}

void MCP3208ADCAddon::setup() {
    s_instance = this;
    spi_ = nullptr;
    spiProfile_ = {};
    spiOk_ = false;
    gateLateBurstActive_ = false;
    csPin_ = -1;
    // Initialize stick channels to center so first frame is neutral.
    const uint16_t center = static_cast<uint16_t>(MCP3208_ADC_MAX_HALF);
    for (StickSnapshot& snapshot : stickSnapshots_) {
        for (uint8_t stick = 0; stick < MCP3208_STICK_COUNT; stick++) {
            snapshot.x[stick] = center;
            snapshot.y[stick] = center;
        }
        snapshot.sequence = 0;
        snapshot.completedTimeUs = 0;
    }
    publishedStickSnapshot_.store(0, std::memory_order_relaxed);
    // Semantic mapping aligned with AnalogInput contract:
    // stick0 -> ANALOG_ADC_1_VRX/VRY, stick1 -> ANALOG_ADC_2_VRX/VRY.
    stick_channels_[0] = {0, 1}; // stick0: CH0/CH1 (VRX/VRY)
    stick_channels_[1] = {7, 6}; // stick1: CH7/CH6 (VRX/VRY)

    // Fixed HML wiring: SPI1 / CS from BoardConfig SPI1_PIN_CS.
    csPin_ = MCP3208_HW_CS_PIN;
    PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(MCP3208_HW_SPI_BLOCK);
    if (!spi || !spi->configured) return;
    spi_ = spi;
    spiProfile_ = spi_->makeBaudrateProfile(MCP3208_SPI_HZ);
    if (!spiProfile_.valid()) return;
    spi_->beginTransaction(spiProfile_, SPI_MSB_FIRST, SPI_MODE0);

    spiOk_ = true;
}

bool MCP3208ADCAddon::prepareSPITransaction() {
    if (!spi_ || !spiOk_ || !spiProfile_.valid()) {
        return false;
    }
    spi_->beginTransaction(spiProfile_, SPI_MSB_FIRST, SPI_MODE0);
    return true;
}

bool MCP3208ADCAddon::sampleStickSnapshot(
    const GateLateAnalogSampleRequest& request
) {
    if (!gateLateBurstActive_ && !prepareSPITransaction()) {
        return false;
    }

    uint16_t xValues[MCP3208_STICK_COUNT] = {};
    uint16_t yValues[MCP3208_STICK_COUNT] = {};
    for (int stick = 0; stick < MCP3208_STICK_COUNT; stick++) {
        const uint8_t x = stick_channels_[stick].x_channel;
        const uint8_t y = stick_channels_[stick].y_channel;
        if (x >= 8 || y >= 8 ||
            !readChannel(x, xValues[stick]) ||
            !readChannel(y, yValues[stick])) {
            return false;
        }
    }
    return publishStickSnapshot(xValues, yValues, request);
}

bool MCP3208ADCAddon::readChannel(
    uint8_t channel,
    uint16_t& value
) {
    if (!spi_ || !spiOk_ || channel >= 8) {
        return false;
    }
    for (int i = 0; i < MCP3208_READ_CHANNELS; i++) {
        if (MCP3208_CHANNELS[i] != channel) {
            continue;
        }
        uint8_t rx[3];
        spi_->select(csPin_);
        spi_->transfer(MCP3208_TX_COMMANDS[i], rx, 3);
        spi_->deselect();
        value = static_cast<uint16_t>(
            ((rx[1] & 0x0F) << 8) | rx[2]);
        return true;
    }
    return false;
}

bool MCP3208ADCAddon::publishStickSnapshot(
    const uint16_t* xValues,
    const uint16_t* yValues,
    const GateLateAnalogSampleRequest& request
) {
    const uint8_t currentIndex =
        publishedStickSnapshot_.load(std::memory_order_relaxed);
    const uint8_t nextIndex = currentIndex ^ 1u;
    StickSnapshot& next = stickSnapshots_[nextIndex];

    for (uint8_t stick = 0; stick < MCP3208_STICK_COUNT; stick++) {
        next.x[stick] = xValues[stick];
        next.y[stick] = yValues[stick];
    }
    next.sequence = stickSnapshots_[currentIndex].sequence + 1u;
    const uint32_t completedTimeUs = time_us_32();
    if (gateDeadlineReached(request, completedTimeUs)) {
        return false;
    }
    next.completedTimeUs = completedTimeUs;
    publishedStickSnapshot_.store(
        nextIndex,
        std::memory_order_release);
    return true;
}

void MCP3208ADCAddon::preprocess() {
    // 早采样已移除：采样统一在 sampleMainLoopGateLateAnalog() 中进行，
    // 该函数在主循环中无条件调用（门控与非门控模式均执行）。
}

bool MCP3208ADCAddon::beginGateLateAnalogBurst() {
    gateLateBurstActive_ = prepareSPITransaction();
    return gateLateBurstActive_;
}

bool MCP3208ADCAddon::sampleGateLateAnalog(
    const GateLateAnalogSampleRequest& request
) {
    return sampleStickSnapshot(request);
}

void MCP3208ADCAddon::endGateLateAnalogBurst() {
    gateLateBurstActive_ = false;
}

uint32_t MCP3208ADCAddon::gateLateAnalogCompletedTimeUs() const {
    const uint8_t snapshotIndex =
        publishedStickSnapshot_.load(std::memory_order_acquire);
    return stickSnapshots_[snapshotIndex].completedTimeUs;
}

void MCP3208ADCAddon::process() {
    // Sample provider only. Unified addons consume cached raw values.
}

void MCP3208ADCAddon::reinit() {
    if (spiOk_) {
        (void)sampleStickSnapshot();
    }
}
