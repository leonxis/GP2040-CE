#ifndef _ADS8332_ADC_H
#define _ADS8332_ADC_H

#include "gpaddon.h"
#include "peripheralmanager.h"

#define ADS8332_ADC_ADDON_NAME "ADS8332 ADC"
#define ADS8332_SPI_HZ 10000000u

class ADS8332ADCAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void preprocess();
    virtual void process();
    virtual void postprocess(bool) {}
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

    void readAllChannelsOptimized(const uint8_t* channels, uint8_t count);
    void readAllChannelsOptimizedUnique(const uint8_t* channels, uint8_t count);
    bool configureADS8332CFR();
    void refreshIIRConfig();
    void resetIIRState();
    uint16_t applyStickIIR(uint8_t channel, uint16_t rawValue);

    PeripheralSPI* spi_ = nullptr;
    int8_t csPin_ = -1;
    int8_t convstPin_ = -1;
    bool spiOk_ = false;
    uint16_t adcValues_[8] = {0};
    uint8_t preprocess_channels_[6] = {0};
    uint8_t preprocess_channel_count_ = 0;
    SamplerStickChannelConfig stick_channels_[2];
    SamplerDividerChannelConfig divider_channels_;
    bool iirEnabled_ = false;
    uint8_t iirShift_ = 1; // alpha = 1 / (2^iirShift_), default 1/2
    bool iirStateInitialized_[4] = {false, false, false, false};
    int32_t iirState_[4] = {0, 0, 0, 0};
    // Cached in setup(): LSM6DSR plugin enabled flag; preprocess restores SPI MODE2 when set (LSM6 uses MODE3 after ADS8332 each frame).
    bool lsm6dsrActiveCached_ = false;
    static ADS8332ADCAddon* s_instance_;
};

#endif
