#ifndef _MCP3208_ADC_H
#define _MCP3208_ADC_H

#include "gpaddon.h"
#include "types.h"
#include "peripheral_spi.h"

#include <atomic>

#define MCP3208_ADC_ADDON_NAME "MCP3208 ADC"

// HML fixed wiring: shares SPI0 and CS GPIO1 with the ADS8332 option.
static constexpr uint8_t MCP3208_HW_SPI_BLOCK = 0;
static constexpr int8_t MCP3208_HW_CS_PIN = 1;

#define MCP3208_SPI_HZ          1500000u

// 通道：CH0=左X, CH1=左Y, CH6=右Y, CH7=右X；CH2/CH5=四档开关；CH3/CH4 悬空不读
#define MCP3208_READ_CHANNELS    6   // 0,1,2,5,6,7
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
    static bool getRawDividerForProcessor(
        uint16_t& leftValue,
        uint16_t& rightValue,
        uint16_t& adcMax,
        bool& leftValid,
        bool& rightValid
    );

    virtual bool available();
    virtual void setup();
    virtual void preprocess();
    virtual void process();
    virtual void postprocess(bool) {}
    virtual void preprocessGateEarly();
    virtual bool isGateLateAnalogProvider() const { return true; }
    virtual GateLateAnalogSource gateLateAnalogSource() const {
        return GateLateAnalogSource::MCP3208;
    }
    virtual bool beginGateLateAnalogBurst();
    virtual bool sampleGateLateAnalog(
        const GateLateAnalogSampleRequest& request);
    virtual void endGateLateAnalogBurst();
    virtual uint32_t gateLateAnalogCompletedTimeUs() const;
    virtual std::string name() { return MCP3208_ADC_ADDON_NAME; }
    virtual void reinit();

private:
    struct SamplerStickChannelConfig {
        uint8_t x_channel;
        uint8_t y_channel;
    };

    struct SamplerDividerChannelConfig {
        uint8_t left_channel;
        uint8_t right_channel;
    };

    struct StickSnapshot {
        uint16_t x[MCP3208_STICK_COUNT];
        uint16_t y[MCP3208_STICK_COUNT];
        uint32_t sequence;
        uint32_t completedTimeUs;
    };

    bool sampleStickSnapshot(
        const GateLateAnalogSampleRequest& request = {});
    bool sampleSwitchChannels();
    bool readChannel(uint8_t channel, uint16_t& value);
    bool prepareSPITransaction();
    bool publishStickSnapshot(
        const uint16_t* xValues,
        const uint16_t* yValues,
        const GateLateAnalogSampleRequest& request = {});

    static MCP3208ADCAddon* s_instance;
    PeripheralSPI* spi_;
    SPIBaudrateProfile spiProfile_;
    int8_t csPin_;            // Chip select GPIO (硬编码)
    uint16_t adcValues_[8];   // Auxiliary CH2/CH5 cache; sticks use published snapshots.
    bool spiOk_;
    SamplerStickChannelConfig stick_channels_[MCP3208_STICK_COUNT];
    SamplerDividerChannelConfig divider_channels_;
    // CH2/CH5 采样降频计数器：仅控制 raw 采样频率，不承担映射/防抖状态
    uint8_t ch25_sample_counter_;
    bool gateLateBurstActive_ = false;
    StickSnapshot stickSnapshots_[2] = {};
    std::atomic<uint8_t> publishedStickSnapshot_ { 0 };
};

#endif
