#ifndef _MCP3208_ADC_H
#define _MCP3208_ADC_H

#include "gpaddon.h"
#include "types.h"
#include "peripheral_spi.h"

#include <atomic>

#define MCP3208_ADC_ADDON_NAME "MCP3208 ADC"

// HML fixed wiring: SPI1 (SCK/TX/RX from webconfig), CS from BoardConfig SPI1_PIN_CS.
static constexpr uint8_t MCP3208_HW_SPI_BLOCK = 1;
static constexpr int8_t MCP3208_HW_CS_PIN = SPI1_PIN_CS;

#define MCP3208_SPI_HZ          1500000u

// 通道：CH0=左X, CH1=左Y, CH6=右Y, CH7=右X；CH3/CH4 悬空不读
#define MCP3208_READ_CHANNELS    4   // 0,1,6,7
#define MCP3208_ADC_MAX         4095
#define MCP3208_ADC_MAX_HALF    (MCP3208_ADC_MAX * 0.5f)
#define MCP3208_STICK_COUNT     2

class MCP3208ADCAddon : public GPAddon {
public:
    // For webconfig/calibration: read current raw ADC for a stick (0=left, 1=right). Returns false if addon not ready.
    static bool getRawStickForWebConfig(
        uint8_t stickNum,
        uint32_t& x,
        uint32_t& y,
        uint32_t& adcMax);
    static bool getRawStickForProcessor(
        uint8_t stickNum,
        uint16_t& x,
        uint16_t& y,
        uint16_t& xCenter,
        uint16_t& yCenter,
        bool& xValid,
        bool& yValid,
        uint16_t& adcMax
    );

    virtual bool available();
    virtual void setup();
    virtual void preprocess();
    virtual void process();
    virtual void postprocess(bool) {}
    virtual std::string name() { return MCP3208_ADC_ADDON_NAME; }
    virtual void reinit();

private:
    struct SamplerStickChannelConfig {
        uint8_t x_channel;
        uint8_t y_channel;
    };

    struct StickSnapshot {
        uint16_t x[MCP3208_STICK_COUNT];
        uint16_t y[MCP3208_STICK_COUNT];
    };

    bool readChannel(uint8_t channel, uint16_t& value);
    bool prepareSPITransaction();
    void publishStickSnapshot(
        const uint16_t* xValues,
        const uint16_t* yValues);

    static MCP3208ADCAddon* s_instance;
    PeripheralSPI* spi_;
    SPIBaudrateProfile spiProfile_;
    int8_t csPin_;            // Chip select GPIO (硬编码)
    bool spiOk_;
    SamplerStickChannelConfig stick_channels_[MCP3208_STICK_COUNT];
    StickSnapshot stickSnapshots_[2] = {};
    std::atomic<uint8_t> publishedStickSnapshot_ { 0 };
};

#endif
