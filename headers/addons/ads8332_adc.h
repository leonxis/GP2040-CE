#ifndef _ADS8332_ADC_H
#define _ADS8332_ADC_H

#include "gpaddon.h"
#include "peripheralmanager.h"

#define ADS8332_ADC_ADDON_NAME "ADS8332 ADC"
#define ADS8332_SPI_HZ 1500000u

class ADS8332ADCAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void preprocess() {}
    virtual void process();
    virtual void postprocess(bool) {}
    virtual std::string name() { return ADS8332_ADC_ADDON_NAME; }
    virtual void reinit();

private:
    uint16_t readChannelRaw(uint8_t channel);
    uint16_t normalizeToJoystick(uint16_t raw) const;

    PeripheralSPI* spi_ = nullptr;
    int8_t csPin_ = -1;
    int8_t convstPin_ = -1;
    bool spiOk_ = false;
    uint32_t cached_joystick_max_ = 65535u;
};

#endif
