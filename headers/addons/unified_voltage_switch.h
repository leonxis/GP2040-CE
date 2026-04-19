#ifndef _UNIFIED_VOLTAGE_SWITCH_H
#define _UNIFIED_VOLTAGE_SWITCH_H

#include "gpaddon.h"
#include "GamepadEnums.h"
#include "config.pb.h"

#define UnifiedVoltageSwitchName "Unified Voltage Switch"

struct VoltageSwitchEntry {
    uint32_t buttonMask;
    uint32_t dpadMask;
    uint8_t keyboardKeyBit;
    uint8_t mouseButtonMask;
};

class UnifiedVoltageSwitchAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void preprocess() {}
    virtual void process();
    virtual void postprocess(bool) {}
    virtual void reinit();
    virtual std::string name() { return UnifiedVoltageSwitchName; }

private:
    void buildMaps();
    void applyLevels(int leftLevel, int rightLevel, class Gamepad* gamepad);
    static void gpioMappingToMasks(const GpioMappingInfo& mapping, uint32_t* outButtons, uint32_t* outDpad);
    static void fillEntryFromMapping(VoltageSwitchEntry& entry, const GpioMappingInfo& mapping, uint32_t buttonMask, uint32_t dpadMask);
    static uint16_t scaledThreshold(float ratio, uint16_t adcMax);

    VoltageSwitchEntry left_map_[4];
    VoltageSwitchEntry right_map_[4];
    int8_t left_stable_level_ = -1;
    int8_t left_pending_level_ = -1;
    uint8_t left_debounce_count_ = 0;
    int8_t right_stable_level_ = -1;
    int8_t right_pending_level_ = -1;
    uint8_t right_debounce_count_ = 0;

    uint32_t last_left_buttons_ = 0;
    uint32_t last_left_dpad_ = 0;
    uint64_t last_left_keyboard_ = 0;
    uint8_t last_left_mouse_ = 0;
    uint32_t last_right_buttons_ = 0;
    uint32_t last_right_dpad_ = 0;
    uint64_t last_right_keyboard_ = 0;
    uint8_t last_right_mouse_ = 0;
};

#endif
