#include "peripheral_spi.h"
#include <hardware/clocks.h>

PeripheralSPI::PeripheralSPI()
    : configured(false),
      initialized(false),
      _RX(0),
      _TX(0),
      _SCK(0),
      _CS(-1),
      _CSActive(-1),
      _SPI(nullptr),
      _Speed(SPI_DEFAULT_SPEED),
      _SpiMode(SPI_MODE0),
      _BitOrder(SPI_MSB_FIRST),
      _Cpol(SPI_CPOL_0),
      _Cpha(SPI_CPHA_0),
      _UseDMA(false),
      _dmaRxChannel(-1),
      _dmaTxChannel(-1),
      _dmaRxBuf(nullptr),
      _dmaTxBuf(nullptr)
{
#ifdef PICO_DEFAULT_SPI_INSTANCE

#if PICO_SDK_VERSION_MAJOR >= 2
    _SPI = PICO_DEFAULT_SPI_INSTANCE();
#elif defined(PICO_DEFAULT_SPI_INSTANCE)
    _SPI = PICO_DEFAULT_SPI_INSTANCE;
#endif

    _TX = PICO_DEFAULT_SPI_TX_PIN;
    _RX = PICO_DEFAULT_SPI_RX_PIN;
    _SCK = PICO_DEFAULT_SPI_SCK_PIN;
    _CS = PICO_DEFAULT_SPI_CSN_PIN;
#endif
}

void PeripheralSPI::setConfig(uint8_t block, uint8_t tx, uint8_t rx, uint8_t sck, uint8_t cs) {
    if (block < NUM_SPIS) {
        _SPI = _hardwareBlocks[block];
        _TX = tx;
        _RX = rx;
        _SCK = sck;
        _CS = cs;
        configured = true;
        setup();
    }
    else {
        // currently not supported
    }
}

void PeripheralSPI::setup() {
    if (initialized) {
        spi_deinit(_SPI);
    }

    spi_init(_SPI, _Speed);
    initialized = true;
    _SpiMode = SPI_MODE0;
    _BitOrder = SPI_MSB_FIRST;
    _Cpol = SPI_CPOL_0;
    _Cpha = SPI_CPHA_0;

    gpio_set_function(_SCK, GPIO_FUNC_SPI);
    gpio_set_function(_TX, GPIO_FUNC_SPI);
    gpio_set_function(_RX, GPIO_FUNC_SPI);
    gpio_pull_up(_RX);

    if (_UseDMA) {
        // DMA configuration - 2 channels (TX/RX)
        _dmaRxChannel = dma_claim_unused_channel(true);
        _dmaRxBuf = new uint8_t[DMA_BUFFER_SIZE]();
        dma_channel_config rxConfig = dma_channel_get_default_config(_dmaRxChannel);
        channel_config_set_transfer_data_size(&rxConfig, DMA_SIZE_8);
        channel_config_set_dreq(&rxConfig, spi_get_dreq(_SPI, false));
        channel_config_set_read_increment(&rxConfig, false);
        dma_channel_configure(
            _dmaRxChannel,         // Channel to be configured
            &rxConfig,             // The configuration we just created
            _dmaRxBuf,             // The initial write address
            &spi_get_hw(_SPI)->dr, // The initial read address
            DMA_BUFFER_SIZE,       // Element count (each element is of size transfer_data_size)
            false                  // Don't start immediately.
        );

        _dmaTxChannel = dma_claim_unused_channel(true);
        _dmaTxBuf = new uint8_t[DMA_BUFFER_SIZE]();
        dma_channel_config txConfig = dma_channel_get_default_config(_dmaTxChannel);
        channel_config_set_transfer_data_size(&txConfig, DMA_SIZE_8);
        channel_config_set_dreq(&txConfig, spi_get_dreq(_SPI, true));
        channel_config_set_write_increment(&txConfig, false);
        dma_channel_configure(
            _dmaTxChannel,         // Channel to be configured
            &txConfig,             // The configuration we just created
            &spi_get_hw(_SPI)->dr, // The initial write address
            _dmaTxBuf,             // The initial read address
            DMA_BUFFER_SIZE,       // Element count (each element is of size transfer_data_size)
            false                  // Don't start immediately.
        );

        dma_start_channel_mask((1u << _dmaTxChannel) | (1u << _dmaRxChannel));
    }
}

void PeripheralSPI::deactivate() {
    if (_UseDMA) {
        dma_channel_unclaim(_dmaRxChannel);
        dma_channel_unclaim(_dmaTxChannel);
        _dmaRxBuf = nullptr;
        _dmaTxBuf = nullptr;
    }
}

void PeripheralSPI::transfer(const uint8_t *tx, uint8_t *rx, size_t count) {
    if (tx == nullptr) {
        spi_read_blocking(_SPI, 0xFF, rx, count);
        return;
    }
    if (rx == nullptr) {
        spi_write_blocking(_SPI, tx, count);
        return;
    }

    spi_write_read_blocking(_SPI, tx, rx, count);
}

uint8_t PeripheralSPI::transfer(uint8_t tx) {
    uint8_t rx;
    spi_write_read_blocking(_SPI, &tx, &rx, 1);
    return rx;
}

uint16_t PeripheralSPI::transfer16(uint16_t tx) {
    uint16_t rx;
    spi_write16_read16_blocking(_SPI, &tx, &rx, 1);
    return rx;
}

void PeripheralSPI::select(int8_t cs) {
    _CSActive = cs > -1 ? cs : _CS;
    if (_CSActive < 0 || _CSActive >= NUM_BANK0_GPIOS) {
        _CSActive = -1;
        return;
    }
    const uint pin = static_cast<uint>(_CSActive);
    if (!_csPinInitialized[pin]) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 1);
        _csPinInitialized[pin] = true;
    }
    gpio_put(pin, 0);
}

void PeripheralSPI::deselect() {
    if (_CSActive > -1) {
        gpio_put(static_cast<uint>(_CSActive), 1);
        _CSActive = -1;
    }
}

void PeripheralSPI::beginTransaction(uint32_t speedMHz, spi_order_t bitOrder, SPIMode spiMode) {
    bool speedChange = speedMHz != _Speed;
    bool needsFullInit = !initialized;
    bool hasFormatChange = bitOrder != _BitOrder || spiMode != _SpiMode;

    if (needsFullInit || speedChange || hasFormatChange) {
        uint32_t flags = save_and_disable_interrupts();

        if (needsFullInit) {
            _Speed = speedMHz;
            spi_init(_SPI, _Speed);
            initialized = true;
        } else if (speedChange) {
            (void)spi_set_baudrate(_SPI, speedMHz);
            _Speed = speedMHz;
        }

        if (hasFormatChange) {
            _BitOrder = bitOrder;
            _SpiMode = spiMode;
            _Cpol = get_cpol(spiMode);
            _Cpha = get_cpha(spiMode);
            spi_set_format(_SPI, 8, _Cpol, _Cpha, _BitOrder);
        }

        restore_interrupts(flags);
    }

}

SPIBaudrateProfile PeripheralSPI::makeBaudrateProfile(uint32_t hz) const {
    SPIBaudrateProfile profile;
    const uint32_t sourceHz = clock_get_hz(clk_peri);
    if (hz == 0 || hz > sourceHz) {
        return profile;
    }

    uint32_t prescale;
    for (prescale = 2; prescale <= 254; prescale += 2) {
        if (sourceHz < prescale * 256ull * hz) {
            break;
        }
    }
    if (prescale > 254) {
        return profile;
    }

    uint32_t postdiv;
    for (postdiv = 256; postdiv > 1; --postdiv) {
        if (sourceHz / (prescale * (postdiv - 1)) > hz) {
            break;
        }
    }

    profile.requestedHz = hz;
    profile.actualHz = sourceHz / (prescale * postdiv);
    profile.prescale = static_cast<uint16_t>(prescale);
    profile.postdiv = static_cast<uint16_t>(postdiv);
    return profile;
}

void PeripheralSPI::applyBaudrateProfile(const SPIBaudrateProfile& profile) {
    const uint32_t enableMask =
        spi_get_hw(_SPI)->cr1 & SPI_SSPCR1_SSE_BITS;
    hw_clear_bits(&spi_get_hw(_SPI)->cr1, SPI_SSPCR1_SSE_BITS);
    spi_get_hw(_SPI)->cpsr = profile.prescale;
    hw_write_masked(
        &spi_get_hw(_SPI)->cr0,
        (profile.postdiv - 1u) << SPI_SSPCR0_SCR_LSB,
        SPI_SSPCR0_SCR_BITS);
    hw_set_bits(&spi_get_hw(_SPI)->cr1, enableMask);
    _Speed = profile.requestedHz;
}

void PeripheralSPI::beginTransaction(
    const SPIBaudrateProfile& profile,
    spi_order_t bitOrder,
    SPIMode spiMode
) {
    if (!profile.valid()) {
        return;
    }

    const bool speedChange = profile.requestedHz != _Speed;
    const bool needsFullInit = !initialized;
    const bool hasFormatChange =
        bitOrder != _BitOrder || spiMode != _SpiMode;
    if (!needsFullInit && !speedChange && !hasFormatChange) {
        return;
    }

    const uint32_t flags = save_and_disable_interrupts();
    if (needsFullInit) {
        (void)spi_init(_SPI, profile.requestedHz);
        initialized = true;
        _Speed = profile.requestedHz;
    } else if (speedChange) {
        applyBaudrateProfile(profile);
    }

    if (hasFormatChange) {
        _BitOrder = bitOrder;
        _SpiMode = spiMode;
        _Cpol = get_cpol(spiMode);
        _Cpha = get_cpha(spiMode);
        spi_set_format(_SPI, 8, _Cpol, _Cpha, _BitOrder);
    }
    restore_interrupts(flags);
}

void PeripheralSPI::endTransaction() {
    (void)0;
}

void PeripheralSPI::setBaudrate(uint32_t hz) {
    if (hz == _Speed)
        return;
    uint32_t flags = save_and_disable_interrupts();
    if (!initialized) {
        _Speed = hz;
        spi_init(_SPI, _Speed);
        initialized = true;
    } else {
        (void)spi_set_baudrate(_SPI, hz);
        _Speed = hz;
    }
    restore_interrupts(flags);
}

void PeripheralSPI::setMode(SPIMode spiMode) {
    if (_SpiMode == spiMode && _BitOrder == SPI_MSB_FIRST) {
        return;
    }
    uint32_t flags = save_and_disable_interrupts();
    _SpiMode = spiMode;
    _BitOrder = SPI_MSB_FIRST;
    _Cpol = get_cpol(spiMode);
    _Cpha = get_cpha(spiMode);
    spi_set_format(_SPI, 8, _Cpol, _Cpha, _BitOrder);
    restore_interrupts(flags);
}
