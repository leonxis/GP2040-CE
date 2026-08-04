#ifndef _ADS8332_ADC_H
#define _ADS8332_ADC_H

#include "gpaddon.h"
#include "peripheralmanager.h"

#include <atomic>

#define ADS8332_ADC_ADDON_NAME "ADS8332 ADC"
#define ADS8332_SPI_HZ 10000000u

// HML 固定接线：SPI0、CS=GPIO1、CONVST=GPIO4（不写入 ADS8332Options）。
static constexpr uint8_t ADS8332_HW_SPI_BLOCK = 0;
static constexpr int8_t ADS8332_HW_CS_PIN = 1;
static constexpr int8_t ADS8332_HW_CONVST_PIN = 4;

class ADS8332ADCAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void preprocess();
    virtual void process();
    virtual void postprocess(bool) {}
    virtual void preprocessGateEarly();
    virtual bool isGateLateAnalogProvider() const { return true; }
    virtual GateLateAnalogSource gateLateAnalogSource() const {
        return GateLateAnalogSource::ADS8332;
    }
    virtual bool beginGateLateAnalogBurst();
    virtual bool sampleGateLateAnalog(
        const GateLateAnalogSampleRequest& request);
    virtual void endGateLateAnalogBurst();
    virtual uint32_t gateLateAnalogCompletedTimeUs() const;
    virtual std::string name() { return ADS8332_ADC_ADDON_NAME; }
    virtual void reinit();
    static bool getRawStickForWebConfig(uint8_t stickNum, uint32_t& x, uint32_t& y, uint32_t& adcMax);
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
private:
    static constexpr uint8_t ADS8332_CHANNEL_COUNT = 8;

    struct SamplerStickChannelConfig {
        uint8_t x_channel;
        uint8_t y_channel;
    };

    struct SamplerDividerChannelConfig {
        uint8_t left_channel;
        uint8_t right_channel;
    };

    struct StickSnapshot {
        uint16_t x[2];
        uint16_t y[2];
        uint32_t sequence;
        uint32_t completedTimeUs;
    };

    bool prepareSPITransaction();
    bool sampleStickSnapshot(
        const GateLateAnalogSampleRequest& request = {});
    bool sampleDividerChannels();
    bool readAllChannelsOptimizedUnique(
        const uint8_t* channels,
        uint8_t count,
        uint16_t* sampledValues);
    bool publishStickSnapshot(
        const uint16_t* sampledValues,
        const GateLateAnalogSampleRequest& request = {});
    bool configureADS8332CFR();

    PeripheralSPI* spi_ = nullptr;
    SPIBaudrateProfile spiProfile_;
    int8_t csPin_ = -1;
    int8_t convstPin_ = -1;
    bool spiOk_ = false;
    uint16_t adcValues_[8] = {0}; // Auxiliary divider cache; sticks use published snapshots.
    uint8_t preprocess_channels_[6] = {0};
    uint8_t preprocess_channel_count_ = 0;
    SamplerStickChannelConfig stick_channels_[2];
    SamplerDividerChannelConfig divider_channels_;
    uint8_t dividerSampleFrameCounter_ = 0;
    bool gateLateBurstActive_ = false;
    StickSnapshot stickSnapshots_[2] = {};
    std::atomic<uint8_t> publishedStickSnapshot_ { 0 };
    static ADS8332ADCAddon* s_instance_;
};

#endif
