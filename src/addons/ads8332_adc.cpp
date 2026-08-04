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

static inline bool gateDeadlineReached(
    const GateLateAnalogSampleRequest& request,
    uint32_t nowUs
) {
    return request.enforceDeadline &&
        static_cast<int32_t>(nowUs - request.deadlineUs) >= 0;
}
} // namespace

ADS8332ADCAddon* ADS8332ADCAddon::s_instance_ = nullptr;

bool ADS8332ADCAddon::available() {
    const ADS8332Options& opts = Storage::getInstance().getAddonOptions().ads8332Options;
    if (!opts.enabled) {
        return false;
    }
    return PeripheralManager::getInstance().isSPIEnabled(ADS8332_HW_SPI_BLOCK);
}

void ADS8332ADCAddon::setup() {
    s_instance_ = this;
    spiOk_ = false;
    spi_ = nullptr;
    spiProfile_ = {};
    dividerSampleFrameCounter_ = 0;
    gateLateBurstActive_ = false;
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
    for (StickSnapshot& snapshot : stickSnapshots_) {
        for (uint8_t stick = 0; stick < 2; stick++) {
            snapshot.x[stick] = ADS8332_RAW_HALF;
            snapshot.y[stick] = ADS8332_RAW_HALF;
        }
        snapshot.sequence = 0;
        snapshot.completedTimeUs = 0;
    }
    publishedStickSnapshot_.store(0, std::memory_order_relaxed);
    csPin_ = ADS8332_HW_CS_PIN;
    convstPin_ = ADS8332_HW_CONVST_PIN;

    PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(ADS8332_HW_SPI_BLOCK);
    if (!spi || !spi->configured || csPin_ < 0) {
        return;
    }

    spi_ = spi;
    spiProfile_ = spi_->makeBaudrateProfile(ADS8332_SPI_HZ);
    if (!spiProfile_.valid()) {
        return;
    }
    spi_->beginTransaction(spiProfile_, SPI_MSB_FIRST, SPI_MODE2);

    if (convstPin_ >= 0) {
        gpio_init(static_cast<uint>(convstPin_));
        gpio_set_dir(static_cast<uint>(convstPin_), GPIO_OUT);
        gpio_put(static_cast<uint>(convstPin_), 1);
    }

    if (!configureADS8332CFR()) {
        return;
    }

    spiOk_ = true;
    (void)sampleDividerChannels();
}

void ADS8332ADCAddon::reinit() {
    if (!spiOk_) {
        return;
    }
    dividerSampleFrameCounter_ = 0;
    (void)sampleDividerChannels();
}

void ADS8332ADCAddon::preprocess() {
    if (!spiOk_) {
        return;
    }
    if (!prepareSPITransaction()) {
        return;
    }

    // Sticks sample every frame; divider keys sample every 4th frame to reduce SPI/CPU load.
    const bool sampleDividerThisFrame = ((dividerSampleFrameCounter_++ & 0x03u) == 0u);
    const uint8_t countThisFrame = sampleDividerThisFrame ? preprocess_channel_count_ : 4u;
    uint16_t sampledValues[ADS8332_CHANNEL_COUNT] = {};
    if (!readAllChannelsOptimizedUnique(
            preprocess_channels_,
            countThisFrame,
            sampledValues)) {
        return;
    }

    (void)publishStickSnapshot(sampledValues);
    if (sampleDividerThisFrame) {
        adcValues_[divider_channels_.left_channel] =
            sampledValues[divider_channels_.left_channel];
        adcValues_[divider_channels_.right_channel] =
            sampledValues[divider_channels_.right_channel];
    }
}

void ADS8332ADCAddon::preprocessGateEarly() {
    if (!spiOk_) {
        return;
    }
    if ((dividerSampleFrameCounter_++ & 0x03u) == 0u) {
        (void)sampleDividerChannels();
    }
}

bool ADS8332ADCAddon::beginGateLateAnalogBurst() {
    gateLateBurstActive_ = prepareSPITransaction();
    return gateLateBurstActive_;
}

bool ADS8332ADCAddon::sampleGateLateAnalog(
    const GateLateAnalogSampleRequest& request
) {
    return sampleStickSnapshot(request);
}

void ADS8332ADCAddon::endGateLateAnalogBurst() {
    gateLateBurstActive_ = false;
}

uint32_t ADS8332ADCAddon::gateLateAnalogCompletedTimeUs() const {
    const uint8_t snapshotIndex =
        publishedStickSnapshot_.load(std::memory_order_acquire);
    return stickSnapshots_[snapshotIndex].completedTimeUs;
}

bool ADS8332ADCAddon::prepareSPITransaction() {
    if (!spi_ || !spiOk_ || !spiProfile_.valid()) {
        return false;
    }
    spi_->beginTransaction(
        spiProfile_,
        SPI_MSB_FIRST,
        SPI_MODE2);
    return true;
}

bool ADS8332ADCAddon::sampleStickSnapshot(
    const GateLateAnalogSampleRequest& request
) {
    if (!gateLateBurstActive_ && !prepareSPITransaction()) {
        return false;
    }

    uint16_t sampledValues[ADS8332_CHANNEL_COUNT] = {};
    if (!readAllChannelsOptimizedUnique(
            preprocess_channels_,
            4,
            sampledValues)) {
        return false;
    }
    return publishStickSnapshot(sampledValues, request);
}

bool ADS8332ADCAddon::sampleDividerChannels() {
    if (!prepareSPITransaction()) {
        return false;
    }

    const uint8_t dividerChannels[2] = {
        divider_channels_.left_channel,
        divider_channels_.right_channel,
    };
    uint16_t sampledValues[ADS8332_CHANNEL_COUNT] = {};
    if (!readAllChannelsOptimizedUnique(
            dividerChannels,
            2,
            sampledValues)) {
        return false;
    }

    adcValues_[divider_channels_.left_channel] =
        sampledValues[divider_channels_.left_channel];
    adcValues_[divider_channels_.right_channel] =
        sampledValues[divider_channels_.right_channel];
    return true;
}

bool ADS8332ADCAddon::readAllChannelsOptimizedUnique(
    const uint8_t* channels,
    uint8_t count,
    uint16_t* sampledValues
) {
    if (!spi_ || !spiOk_ || channels == nullptr ||
        sampledValues == nullptr || count == 0) {
        return false;
    }
    for (uint8_t i = 0; i < count; i++) {
        if (channels[i] >= ADS8332_CHANNEL_COUNT) {
            return false;
        }
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
        sampledValues[ch] = rawValue;
        if (i + 1 < count) {
            ads8332ArmChannelAndKick(spi_, csPin_, convstPin_, channels[i + 1]);
        }
    }
    return true;
}

bool ADS8332ADCAddon::publishStickSnapshot(
    const uint16_t* sampledValues,
    const GateLateAnalogSampleRequest& request
) {
    const uint8_t currentIndex =
        publishedStickSnapshot_.load(std::memory_order_relaxed);
    const uint8_t nextIndex = currentIndex ^ 1u;
    StickSnapshot& next = stickSnapshots_[nextIndex];

    for (uint8_t stick = 0; stick < 2; stick++) {
        next.x[stick] =
            sampledValues[stick_channels_[stick].x_channel];
        next.y[stick] =
            sampledValues[stick_channels_[stick].y_channel];
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
    (void)s_instance_->sampleStickSnapshot();
    const uint8_t snapshotIndex =
        s_instance_->publishedStickSnapshot_.load(
            std::memory_order_acquire);
    const StickSnapshot& snapshot =
        s_instance_->stickSnapshots_[snapshotIndex];
    x = snapshot.x[stickNum];
    y = snapshot.y[stickNum];
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
    const uint8_t snapshotIndex =
        s_instance_->publishedStickSnapshot_.load(
            std::memory_order_acquire);
    const StickSnapshot& snapshot =
        s_instance_->stickSnapshots_[snapshotIndex];
    x = snapshot.x[stickNum];
    y = snapshot.y[stickNum];
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
