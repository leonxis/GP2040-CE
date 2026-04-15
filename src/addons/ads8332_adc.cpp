#include "addons/ads8332_adc.h"

#include "storagemanager.h"

#include "hardware/gpio.h"

namespace {
static constexpr uint16_t ADS8332_RAW_MAX = 65535u;
static constexpr uint16_t ADS8332_RAW_HALF = ADS8332_RAW_MAX / 2u;

// CMR command format (manual channel select).
// This is intentionally minimal for initial bring-up.
static inline uint16_t ads8332ChannelCommand(uint8_t channel) {
    return static_cast<uint16_t>(0x8000u | ((channel & 0x07u) << 8));
}
} // namespace

ADS8332ADCAddon* ADS8332ADCAddon::s_instance_ = nullptr;

bool ADS8332ADCAddon::available() {
    const ADS8332Options& opts = Storage::getInstance().getAddonOptions().ads8332Options;
    if (!opts.enabled) {
        return false;
    }
    uint8_t block = opts.has_spiBlock ? static_cast<uint8_t>(opts.spiBlock) : 0;
    return PeripheralManager::getInstance().isSPIEnabled(block);
}

void ADS8332ADCAddon::setup() {
    s_instance_ = this;
    spiOk_ = false;
    spi_ = nullptr;
    csPin_ = -1;
    convstPin_ = -1;
    stick_channels_[0] = {0, 1}; // left
    stick_channels_[1] = {7, 6}; // right
    divider_channels_ = {2, 5};
    for (int i = 0; i < 8; i++) {
        adcValues_[i] = ADS8332_RAW_HALF;
    }
    const ADS8332Options& opts = Storage::getInstance().getAddonOptions().ads8332Options;
    uint8_t block = opts.has_spiBlock ? static_cast<uint8_t>(opts.spiBlock) : 0;

    if (opts.has_csPin) {
        csPin_ = static_cast<int8_t>(opts.csPin);
    }
    if (opts.has_convstPin) {
        convstPin_ = static_cast<int8_t>(opts.convstPin);
    }

    PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(block);
    if (!spi || !spi->configured || csPin_ < 0) {
        return;
    }

    spi_ = spi;
    // Align SPI mode with LSM6DSR addon configuration.
    spi_->beginTransaction(ADS8332_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
    spi_->setBaudrate(ADS8332_SPI_HZ);

    if (convstPin_ >= 0) {
        gpio_init(static_cast<uint>(convstPin_));
        gpio_set_dir(static_cast<uint>(convstPin_), GPIO_OUT);
        gpio_put(static_cast<uint>(convstPin_), 1);
    }

    spiOk_ = true;
    const uint8_t dividerChannels[2] = {divider_channels_.left_channel, divider_channels_.right_channel};
    readSelectedChannels(dividerChannels, 2);
}

void ADS8332ADCAddon::reinit() {
    if (!spiOk_) {
        return;
    }
    const uint8_t dividerChannels[2] = {divider_channels_.left_channel, divider_channels_.right_channel};
    readSelectedChannels(dividerChannels, 2);
}

void ADS8332ADCAddon::preprocess() {
    if (!spiOk_) {
        return;
    }
    const uint8_t channels[6] = {
        stick_channels_[0].x_channel,
        stick_channels_[0].y_channel,
        stick_channels_[1].x_channel,
        stick_channels_[1].y_channel,
        divider_channels_.left_channel,
        divider_channels_.right_channel
    };
    readSelectedChannels(channels, 6);
}

uint16_t ADS8332ADCAddon::readChannelRaw(uint8_t channel) {
    if (!spi_ || !spiOk_ || channel >= 8) {
        return ADS8332_RAW_MAX / 2u;
    }

    // Global CONVST pulse (if configured).
    if (convstPin_ >= 0) {
        gpio_put(static_cast<uint>(convstPin_), 0);
        gpio_put(static_cast<uint>(convstPin_), 1);
    }

    // First transfer writes channel command, second transfer fetches conversion data.
    spi_->select(csPin_);
    (void)spi_->transfer16(ads8332ChannelCommand(channel));
    uint16_t value = spi_->transfer16(0x0000u);
    spi_->deselect();

    return value;
}

void ADS8332ADCAddon::readSelectedChannels(const uint8_t* channels, uint8_t count) {
    bool sampled[8] = {};
    for (uint8_t i = 0; i < count; i++) {
        const uint8_t channel = channels[i];
        if (channel >= 8 || sampled[channel]) {
            continue;
        }
        adcValues_[channel] = readChannelRaw(channel);
        sampled[channel] = true;
    }
}

void ADS8332ADCAddon::process() {
    // Sample provider only. Unified addons consume cached raw values.
}

bool ADS8332ADCAddon::getRawStickForWebConfig(uint8_t stickNum, uint32_t& x, uint32_t& y, uint32_t& adcMax) {
    if (s_instance_ == nullptr || !s_instance_->spiOk_ || stickNum > 1) {
        return false;
    }

    adcMax = ADS8332_RAW_MAX;
    s_instance_->preprocess();
    const SamplerStickChannelConfig& channels = s_instance_->stick_channels_[stickNum];
    x = s_instance_->adcValues_[channels.x_channel];
    y = s_instance_->adcValues_[channels.y_channel];
    return true;
}

bool ADS8332ADCAddon::getRawStickForProcessor(
    uint8_t stickNum,
    uint16_t& x,
    uint16_t& y,
    uint16_t& xCenter,
    uint16_t& yCenter,
    bool& xValid,
    bool& yValid,
    uint16_t& adcMax
) {
    if (s_instance_ == nullptr || !s_instance_->spiOk_ || stickNum > 1) {
        return false;
    }
    const SamplerStickChannelConfig& channels = s_instance_->stick_channels_[stickNum];
    x = s_instance_->adcValues_[channels.x_channel];
    y = s_instance_->adcValues_[channels.y_channel];
    xCenter = ADS8332_RAW_HALF;
    yCenter = ADS8332_RAW_HALF;
    xValid = true;
    yValid = true;
    adcMax = ADS8332_RAW_MAX;
    return true;
}

bool ADS8332ADCAddon::getRawDividerForProcessor(
    uint16_t& leftValue,
    uint16_t& rightValue,
    uint16_t& adcMax,
    bool& leftValid,
    bool& rightValid
) {
    if (s_instance_ == nullptr || !s_instance_->spiOk_) {
        return false;
    }
    leftValue = s_instance_->adcValues_[s_instance_->divider_channels_.left_channel];
    rightValue = s_instance_->adcValues_[s_instance_->divider_channels_.right_channel];
    adcMax = ADS8332_RAW_MAX;
    leftValid = true;
    rightValid = true;
    return true;
}
