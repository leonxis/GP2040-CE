#include "addons/mcp3208_adc.h"
#include "config.pb.h"
#include "storagemanager.h"
#include "peripheralmanager.h"

static const uint8_t CH25_SAMPLE_DIVIDER = 4;    // CH2/CH5 降采样：每 N 帧读取一次

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
    const MCP3208Options& opts = Storage::getInstance().getAddonOptions().mcp3208Options;
    if (!opts.enabled || !opts.has_csPin)
        return false;
    uint8_t block = opts.has_spiBlock ? (uint8_t)opts.spiBlock : 0;
    return PeripheralManager::getInstance().isSPIEnabled(block);
}

// Static instance for webconfig to read raw stick values (no AddonManager dependency)
MCP3208ADCAddon* MCP3208ADCAddon::s_instance = nullptr;

bool MCP3208ADCAddon::getRawStickForWebConfig(uint8_t stickNum, uint16_t& x, uint16_t& y) {
    if (s_instance == nullptr || !s_instance->spiOk_ || stickNum >= MCP3208_STICK_COUNT) {
        return false;
    }
    // Web calibration canvas only needs stick channels.
    s_instance->readStickChannels();
    x = (stickNum == 0) ? s_instance->adcValues_[0] : s_instance->adcValues_[7];
    y = (stickNum == 0) ? s_instance->adcValues_[1] : s_instance->adcValues_[6];
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
    const SamplerStickChannelConfig& channels = s_instance->stick_channels_[stickNum];
    x = s_instance->adcValues_[channels.x_channel];
    y = s_instance->adcValues_[channels.y_channel];
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
    spiOk_ = false;
    csPin_ = -1;
    // Initialize stick channels to center so first frame is neutral.
    const uint16_t center = static_cast<uint16_t>(MCP3208_ADC_MAX_HALF);
    for (int i = 0; i < 8; i++) adcValues_[i] = 0;
    adcValues_[0] = adcValues_[1] = adcValues_[6] = adcValues_[7] = center;
    ch25_sample_counter_ = 0;
    // Semantic mapping aligned with AnalogInput contract:
    // stick0 -> ANALOG_ADC_1_VRX/VRY, stick1 -> ANALOG_ADC_2_VRX/VRY.
    stick_channels_[0] = {0, 1}; // stick0: CH0/CH1 (VRX/VRY)
    stick_channels_[1] = {7, 6}; // stick1: CH7/CH6 (VRX/VRY)
    divider_channels_ = {2, 5};  // divider left/right: CH2/CH5

    const MCP3208Options& opts = Storage::getInstance().getAddonOptions().mcp3208Options;
    uint8_t block = opts.has_spiBlock ? (uint8_t)opts.spiBlock : 0;
    if (!opts.has_csPin) return;
    csPin_ = (int8_t)opts.csPin;
    PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(block);
    if (!spi || !spi->configured) return;
    spi_ = spi;
    spi_->beginTransaction(MCP3208_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
    spi_->setBaudrate(MCP3208_SPI_HZ);

    spiOk_ = true;
    readSwitchChannels();
}

void MCP3208ADCAddon::readStickChannels() {
    bool sampled[8] = {};
    for (int stick = 0; stick < MCP3208_STICK_COUNT; stick++) {
        const uint8_t x = stick_channels_[stick].x_channel;
        const uint8_t y = stick_channels_[stick].y_channel;
        if (x < 8 && !sampled[x] && readChannel(x)) {
            sampled[x] = true;
        }
        if (y < 8 && !sampled[y] && readChannel(y)) {
            sampled[y] = true;
        }
    }
}

void MCP3208ADCAddon::readSwitchChannels() {
    if (divider_channels_.left_channel < 8) {
        (void)readChannel(divider_channels_.left_channel);
    }
    if (divider_channels_.right_channel < 8) {
        (void)readChannel(divider_channels_.right_channel);
    }
}

bool MCP3208ADCAddon::readChannel(uint8_t channel) {
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
        adcValues_[channel] = static_cast<uint16_t>(((rx[1] & 0x0F) << 8) | rx[2]);
        return true;
    }
    return false;
}

void MCP3208ADCAddon::preprocess() {
    if (!spiOk_) return;
    readStickChannels();
    if (++ch25_sample_counter_ >= CH25_SAMPLE_DIVIDER) {
        ch25_sample_counter_ = 0;
        readSwitchChannels();
    }
}

void MCP3208ADCAddon::process() {
    // Sample provider only. Unified addons consume cached raw values.
}

void MCP3208ADCAddon::reinit() {
    ch25_sample_counter_ = 0;
    if (spiOk_) {
        readSwitchChannels();
    }
}
