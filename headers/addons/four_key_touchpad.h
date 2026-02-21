#ifndef _FOUR_KEY_TOUCHPAD_H
#define _FOUR_KEY_TOUCHPAD_H

#include "gpaddon.h"
#include "BoardConfig.h"
#include "config.pb.h"

#define FOUR_KEY_TOUCHPAD_ADDON_NAME "4键触摸板"

// 预解析映射：运行时仅 OR mask，复杂类型(ANALOG/MENU/KEYBOARD) 再进 switch
struct FastMapping {
    uint32_t buttonMask;
    uint32_t dpadMask;
    uint32_t auxMask;
    bool isComplex;
    const GpioMappingInfo* originalMapping;
};

// BS814A-2: DATA = I2C1 SCL (MCU reads), SCK = I2C1 SDA (MCU drives). Bit-bang 4 bits; low = pressed; map to A1–A4.

class FourKeyTouchpadAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void process();
    virtual void preprocess();
    virtual void postprocess(bool) {}
    virtual std::string name() { return FOUR_KEY_TOUCHPAD_ADDON_NAME; }
    virtual void reinit();
private:
    void buildMappings();

    int32_t pin_sck = -1;
    int32_t pin_data = -1;
    FastMapping fastMappings[4];
    // 使能键 3ms 防抖（低有效）
    bool enableRawLast = false;
    uint32_t enableChangeTime = 0;
    bool enableStable = false;
    uint8_t lastKeyNibble = 0x0F;
    uint32_t lastPollTime = 0;
    uint8_t partialByte = 0;
    uint8_t readPhase = 0;
};

#endif
