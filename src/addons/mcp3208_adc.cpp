#include "addons/mcp3208_adc.h"
#include "config.pb.h"
#include "storagemanager.h"
#include "peripheralmanager.h"

#include "pico/time.h"

static const uint8_t MCP3208_CHANNELS[MCP3208_READ_CHANNELS] = {0, 1, 2, 5, 6, 7};
static const uint8_t MCP3208_TX_COMMANDS[MCP3208_READ_CHANNELS][3] = {
    {0x06, 0x00, 0x00}, // CH0
    {0x06, 0x40, 0x00}, // CH1
    {0x06, 0x80, 0x00}, // CH2
    {0x07, 0x40, 0x00}, // CH5
    {0x07, 0x80, 0x00}, // CH6
    {0x07, 0xC0, 0x00}, // CH7
};

bool MCP3208ADCAddon::available() {
    const AddonOptions& addonOptions =
        Storage::getInstance().getAddonOptions();
    if (!addonOptions.mcp3208Options.enabled ||
        addonOptions.ads8332Options.enabled) {
        return false;
    }
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
    s_instance->process();
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

bool MCP3208ADCAddon::getRawDividerForProcessor(
    uint16_t& leftValue,
    uint16_t& rightValue,
    uint16_t& adcMax,
    bool& leftValid,
    bool& rightValid
) {
    if (s_instance == nullptr || !s_instance->spiOk_) {
        return false;
    }
    leftValue = s_instance->adcValues_[s_instance->divider_channels_.left_channel];
    rightValue = s_instance->adcValues_[s_instance->divider_channels_.right_channel];
    adcMax = MCP3208_ADC_MAX;
    leftValid = true;
    rightValid = true;
    return true;
}

void MCP3208ADCAddon::setup() {
    s_instance = this;
    spi_ = nullptr;
    spiProfile_ = {};
    spiOk_ = false;
    csPin_ = -1;
    // Initialize stick channels to center so first frame is neutral.
    const uint16_t center = static_cast<uint16_t>(MCP3208_ADC_MAX_HALF);
    for (int i = 0; i < 8; i++) adcValues_[i] = 0;
    for (StickSnapshot& snapshot : stickSnapshots_) {
        for (uint8_t stick = 0; stick < MCP3208_STICK_COUNT; stick++) {
            snapshot.x[stick] = center;
            snapshot.y[stick] = center;
        }
    }
    publishedStickSnapshot_.store(0, std::memory_order_relaxed);
    ch25_sample_counter_ = 0;
    // Semantic mapping aligned with AnalogInput contract:
    // stick0 -> ANALOG_ADC_1_VRX/VRY, stick1 -> ANALOG_ADC_2_VRX/VRY.
    stick_channels_[0] = {0, 1}; // stick0: CH0/CH1 (VRX/VRY)
    stick_channels_[1] = {7, 6}; // stick1: CH7/CH6 (VRX/VRY)
    divider_channels_ = {2, 5};  // divider left/right: CH2/CH5

    // Fixed CS wiring shared with the ADS8332 option.
    csPin_ = MCP3208_HW_CS_PIN;
    PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(MCP3208_HW_SPI_BLOCK);
    if (!spi || !spi->configured) return;
    spi_ = spi;
    spiProfile_ = spi_->makeBaudrateProfile(MCP3208_SPI_HZ);
    if (!spiProfile_.valid()) return;
    spi_->beginTransaction(spiProfile_, SPI_MSB_FIRST, SPI_MODE0);

    spiOk_ = true;
    // First scan samples all six channels (frame counter starts at 0).
    process();
}

bool MCP3208ADCAddon::prepareSPITransaction() {
    if (!spi_ || !spiOk_ || !spiProfile_.valid()) {
        return false;
    }
    spi_->beginTransaction(spiProfile_, SPI_MSB_FIRST, SPI_MODE0);
    return true;
}

void MCP3208ADCAddon::process() {
    if (!prepareSPITransaction()) {
        return;
    }

    // One scan per frame (each channel toggles CS itself). Every 4th frame the
    // divider channels (CH2/CH5) are included in the same scan; other frames
    // only read the four stick channels. Values are indexed by channel number.
    static constexpr uint8_t STICK_CHANNEL_COUNT = 4;
    const uint8_t stickChannels[STICK_CHANNEL_COUNT] = {
        stick_channels_[0].x_channel,
        stick_channels_[0].y_channel,
        stick_channels_[1].x_channel,
        stick_channels_[1].y_channel,
    };
    const bool sampleDividerThisFrame =
        ((ch25_sample_counter_++ & 0x03u) == 0u);

    uint16_t values[8] = {};
    if (sampleDividerThisFrame) {
        if (!readChannelsBatched(
                MCP3208_CHANNELS,
                MCP3208_READ_CHANNELS,
                values)) {
            return;
        }
        adcValues_[divider_channels_.left_channel] =
            values[divider_channels_.left_channel];
        adcValues_[divider_channels_.right_channel] =
            values[divider_channels_.right_channel];
    } else if (!readChannelsBatched(
                   stickChannels,
                   STICK_CHANNEL_COUNT,
                   values)) {
        return;
    }

    uint16_t xValues[MCP3208_STICK_COUNT] = {};
    uint16_t yValues[MCP3208_STICK_COUNT] = {};
    for (uint8_t stick = 0; stick < MCP3208_STICK_COUNT; stick++) {
        xValues[stick] = values[stick_channels_[stick].x_channel];
        yValues[stick] = values[stick_channels_[stick].y_channel];
    }
    publishStickSnapshot(xValues, yValues);
}

bool MCP3208ADCAddon::readChannelsBatched(
    const uint8_t* channels,
    uint8_t count,
    uint16_t* values
) {
    if (!spi_ || !spiOk_ || channels == nullptr ||
        values == nullptr || count == 0 ||
        count > MCP3208_READ_CHANNELS) {
        return false;
    }

    // Each channel needs its own CS falling edge: per the MCP3204/3208
    // datasheet, once a data transfer completes the device ignores further
    // start bits while CS stays low (it clocks out LSB-first data then zeros),
    // so a multi-channel scan is one CS session per channel back to back.
    for (uint8_t i = 0; i < count; i++) {
        int8_t commandIndex = -1;
        for (uint8_t j = 0; j < MCP3208_READ_CHANNELS; j++) {
            if (MCP3208_CHANNELS[j] == channels[i]) {
                commandIndex = static_cast<int8_t>(j);
                break;
            }
        }
        if (commandIndex < 0) {
            return false;
        }
        uint8_t rx[3] = {};
        spi_->select(csPin_);
        spi_->transfer(MCP3208_TX_COMMANDS[commandIndex], rx, 3);
        spi_->deselect();
        values[channels[i]] = static_cast<uint16_t>(
            ((rx[1] & 0x0F) << 8) | rx[2]);
    }
    return true;
}

void MCP3208ADCAddon::publishStickSnapshot(
    const uint16_t* xValues,
    const uint16_t* yValues
) {
    const uint8_t currentIndex =
        publishedStickSnapshot_.load(std::memory_order_relaxed);
    const uint8_t nextIndex = currentIndex ^ 1u;
    StickSnapshot& next = stickSnapshots_[nextIndex];

    for (uint8_t stick = 0; stick < MCP3208_STICK_COUNT; stick++) {
        next.x[stick] = xValues[stick];
        next.y[stick] = yValues[stick];
    }
    publishedStickSnapshot_.store(
        nextIndex,
        std::memory_order_release);
}

void MCP3208ADCAddon::reinit() {
    if (!spiOk_) {
        return;
    }
    // Force the next scan to include the divider channels.
    ch25_sample_counter_ = 0;
    process();
}
