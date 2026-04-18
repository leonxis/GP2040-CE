#include "addons/ads8332_adc.h"

#include "storagemanager.h"

#include "hardware/gpio.h"
#include "pico/time.h"

namespace {
static constexpr uint16_t ADS8332_RAW_MAX = 65535u;
static constexpr uint16_t ADS8332_RAW_HALF = ADS8332_RAW_MAX / 2u;
static constexpr uint8_t ADS8332_CHANNEL_MAX = 7u;
static constexpr uint16_t ADS8332_CMD_READ_DATA = 0xD000u; // Table 4: Dh
static constexpr uint16_t ADS8332_CMD_WRITE_CFR = 0xE000u; // Table 4: Eh
static constexpr uint16_t ADS8332_CMD_READ_CFR = 0xC000u; // Table 4: Ch
static constexpr uint16_t ADS8332_CFR_VALUE = 0x06FDu;

// CMR command format (manual channel select).
// This is intentionally minimal for initial bring-up.
static inline uint16_t ads8332ChannelCommand(uint8_t channel) {
    return static_cast<uint16_t>((channel & ADS8332_CHANNEL_MAX) << 12);
}

static inline uint16_t ads8332WriteCfrCommand(uint16_t cfrValue) {
    return static_cast<uint16_t>(ADS8332_CMD_WRITE_CFR | (cfrValue & 0x0FFFu));
}

// ADS8332 uses 16-bit SPI words (MSB first). PeripheralSPI stays in 8-bit mode, so each word is two bytes.
static inline void ads8332SpiSendWord(PeripheralSPI* spi, uint16_t cmd) {
    const uint8_t tx[2] = {
        static_cast<uint8_t>(cmd >> 8),
        static_cast<uint8_t>(cmd & 0xFFu),
    };
    spi->transfer(tx, nullptr, 2);
}

static inline uint16_t ads8332SpiXferWord(PeripheralSPI* spi, uint16_t cmd) {
    const uint8_t tx[2] = {
        static_cast<uint8_t>(cmd >> 8),
        static_cast<uint8_t>(cmd & 0xFFu),
    };
    uint8_t rx[2] = {0, 0};
    spi->transfer(tx, rx, 2);
    return static_cast<uint16_t>((static_cast<uint16_t>(rx[0]) << 8) | rx[1]);
}

static inline void ads8332KickConversion(int8_t convstPin) {
    if (convstPin < 0) {
        return;
    }
    gpio_put(static_cast<uint>(convstPin), 0);
    sleep_us(1);
    gpio_put(static_cast<uint>(convstPin), 1);
    // Allow minimum conversion/acquisition time before the next SPI read clocks result.
    sleep_us(2);
}

static inline void ads8332ArmChannelAndKick(PeripheralSPI* spi, int8_t csPin, int8_t convstPin, uint8_t channel) {
    spi->select(csPin);
    ads8332SpiSendWord(spi, ads8332ChannelCommand(channel));
    spi->deselect();
    ads8332KickConversion(convstPin);
}

static inline uint16_t ads8332ReadDataWord(PeripheralSPI* spi, int8_t csPin) {
    spi->select(csPin);
    const uint16_t rawValue = ads8332SpiXferWord(spi, ADS8332_CMD_READ_DATA);
    spi->deselect();
    return rawValue;
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
    lsm6dsrActiveCached_ = false;
    dividerSampleFrameCounter_ = 0;
    csPin_ = -1;
    convstPin_ = -1;
    // Semantic mapping aligned with AnalogInput:
    // stick0 -> ANALOG_ADC_1_VRX/VRY, stick1 -> ANALOG_ADC_2_VRX/VRY.
    // User-confirmed ADS8332 channel numbering is internal 0-based:
    // IN1->1, IN2->2, IN5->5, IN6->6, IN3->3, IN4->4.
    stick_channels_[0] = {2, 1}; // stick0: VRX/VRY (swapped)
    stick_channels_[1] = {6, 5}; // stick1: VRX/VRY (swapped)
    divider_channels_ = {3, 4};  // left/right divider keys
    preprocess_channels_[0] = stick_channels_[0].x_channel;
    preprocess_channels_[1] = stick_channels_[0].y_channel;
    preprocess_channels_[2] = stick_channels_[1].x_channel;
    preprocess_channels_[3] = stick_channels_[1].y_channel;
    preprocess_channels_[4] = divider_channels_.left_channel;
    preprocess_channels_[5] = divider_channels_.right_channel;
    preprocess_channel_count_ = 6;
    for (int i = 0; i < ADS8332ADCAddon::ADS8332_CHANNEL_COUNT; i++) {
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
    lsm6dsrActiveCached_ = Storage::getInstance().getAddonOptions().lsm6dsrOptions.enabled;
    spi_->setBaudrate(ADS8332_SPI_HZ);
    spi_->setMode(SPI_MODE2);

    if (convstPin_ >= 0) {
        gpio_init(static_cast<uint>(convstPin_));
        gpio_set_dir(static_cast<uint>(convstPin_), GPIO_OUT);
        gpio_put(static_cast<uint>(convstPin_), 1);
    }

    if (!configureADS8332CFR()) {
        return;
    }

    spiOk_ = true;
    const uint8_t dividerChannels[2] = {divider_channels_.left_channel, divider_channels_.right_channel};
    readAllChannelsOptimized(dividerChannels, 2);
}

void ADS8332ADCAddon::reinit() {
    if (!spiOk_) {
        return;
    }
    dividerSampleFrameCounter_ = 0;
    const uint8_t dividerChannels[2] = {divider_channels_.left_channel, divider_channels_.right_channel};
    readAllChannelsOptimized(dividerChannels, 2);
}

void ADS8332ADCAddon::preprocess() {
    if (!spiOk_) {
        return;
    }
    if (lsm6dsrActiveCached_) {
        spi_->setMode(SPI_MODE2);
    }
    // Sticks sample every frame; divider keys sample every 4th frame to reduce SPI/CPU load.
    const bool sampleDividerThisFrame = ((dividerSampleFrameCounter_++ & 0x03u) == 0u);
    const uint8_t countThisFrame = sampleDividerThisFrame ? preprocess_channel_count_ : 4u;
    readAllChannelsOptimizedUnique(preprocess_channels_, countThisFrame);
}

void ADS8332ADCAddon::readAllChannelsOptimized(const uint8_t* channels, uint8_t count) {
    if (!spi_ || !spiOk_ || channels == nullptr || count == 0) {
        return;
    }

    // Dedupe requested channels so each conversion slot is used once.
    uint8_t uniqueChannels[ADS8332_CHANNEL_COUNT] = {};
    uint8_t uniqueCount = 0;
    bool sampled[ADS8332_CHANNEL_COUNT] = {};
    for (uint8_t i = 0; i < count; i++) {
        const uint8_t channel = channels[i];
        if (channel >= ADS8332_CHANNEL_COUNT || sampled[channel]) {
            continue;
        }
        sampled[channel] = true;
        uniqueChannels[uniqueCount++] = channel;
    }
    if (uniqueCount == 0) {
        return;
    }

    readAllChannelsOptimizedUnique(uniqueChannels, uniqueCount);
}

void ADS8332ADCAddon::readAllChannelsOptimizedUnique(const uint8_t* channels, uint8_t count) {
    if (!spi_ || !spiOk_ || channels == nullptr || count == 0) {
        return;
    }
    // Fixed sequence per channel: discard first conversion, keep second (ADC settling).
    // READ DATA does not alter MUX; a CMR write is only required when channel changes.
    // Prime first channel so the first loop body read clocks out its discard sample.
    ads8332ArmChannelAndKick(spi_, csPin_, convstPin_, channels[0]);
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t ch = channels[i];
        (void)ads8332ReadDataWord(spi_, csPin_);
        ads8332KickConversion(convstPin_);
        const uint16_t rawValue = ads8332ReadDataWord(spi_, csPin_);
        adcValues_[ch] = rawValue;
        if (i + 1 < count) {
            ads8332ArmChannelAndKick(spi_, csPin_, convstPin_, channels[i + 1]);
        }
    }
}

bool ADS8332ADCAddon::configureADS8332CFR() {
    if (!spi_) {
        return false;
    }
    spi_->select(csPin_);
    ads8332SpiSendWord(spi_, ads8332WriteCfrCommand(ADS8332_CFR_VALUE));
    spi_->deselect();
    sleep_ms(1);

    spi_->select(csPin_);
    const uint16_t readback = ads8332SpiXferWord(spi_, ADS8332_CMD_READ_CFR);
    spi_->deselect();

    return (readback & 0x0FFFu) == (ADS8332_CFR_VALUE & 0x0FFFu);
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
