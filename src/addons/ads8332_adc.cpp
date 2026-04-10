#include "addons/ads8332_adc.h"

#include "storagemanager.h"
#include "drivermanager.h"

#include "hardware/gpio.h"

// ADS8332: lightweight bring-up path.
// For now we only provide basic joystick output and keep advanced features
// (calibration/curves/voltage-stage key mapping) for a later iteration.
namespace {
static constexpr uint16_t ADS8332_RAW_MAX = 65535u;

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
    cached_joystick_max_ = 65535u;

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
    // TI forum examples commonly use MODE2 for ADS8332.
    spi_->beginTransaction(ADS8332_SPI_HZ, SPI_MSB_FIRST, SPI_MODE2);
    spi_->setBaudrate(ADS8332_SPI_HZ);

    if (convstPin_ >= 0) {
        gpio_init(static_cast<uint>(convstPin_));
        gpio_set_dir(static_cast<uint>(convstPin_), GPIO_OUT);
        gpio_put(static_cast<uint>(convstPin_), 1);
    }

    GPDriver* driver = DriverManager::getInstance().getDriver();
    if (driver != nullptr) {
        cached_joystick_max_ = static_cast<uint32_t>(driver->GetJoystickMidValue()) * 2u;
    }

    spiOk_ = true;
}

void ADS8332ADCAddon::reinit() {
    GPDriver* driver = DriverManager::getInstance().getDriver();
    cached_joystick_max_ = 65535u;
    if (driver != nullptr) {
        cached_joystick_max_ = static_cast<uint32_t>(driver->GetJoystickMidValue()) * 2u;
    }
}

uint16_t ADS8332ADCAddon::readChannelRaw(uint8_t channel) {
    if (!spi_ || !spiOk_) {
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

uint16_t ADS8332ADCAddon::normalizeToJoystick(uint16_t raw) const {
    const uint32_t maxOut = cached_joystick_max_ ? cached_joystick_max_ : 65535u;
    const uint32_t scaled = (static_cast<uint32_t>(raw) * maxOut) / ADS8332_RAW_MAX;
    return static_cast<uint16_t>(scaled > 0xFFFFu ? 0xFFFFu : scaled);
}

void ADS8332ADCAddon::process() {
    if (!spiOk_) {
        return;
    }

    // Minimal mapping:
    // CH0 -> LX, CH1 -> LY, CH6 -> RY, CH7 -> RX
    const uint16_t ch0 = readChannelRaw(0);
    const uint16_t ch1 = readChannelRaw(1);
    const uint16_t ch6 = readChannelRaw(6);
    const uint16_t ch7 = readChannelRaw(7);

    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    gamepad->state.lx = normalizeToJoystick(ch0);
    gamepad->state.ly = normalizeToJoystick(ch1);
    gamepad->state.ry = normalizeToJoystick(ch6);
    gamepad->state.rx = normalizeToJoystick(ch7);
}

bool ADS8332ADCAddon::getRawStickForWebConfig(uint8_t stickNum, uint32_t& x, uint32_t& y, uint32_t& adcMax) {
    if (s_instance_ == nullptr || !s_instance_->spiOk_ || stickNum > 1) {
        return false;
    }

    adcMax = ADS8332_RAW_MAX;
    if (stickNum == 0) {
        x = s_instance_->readChannelRaw(0);
        y = s_instance_->readChannelRaw(1);
    } else {
        x = s_instance_->readChannelRaw(7);
        y = s_instance_->readChannelRaw(6);
    }
    return true;
}
