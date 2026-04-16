#ifndef _UNIFIED_JOYSTICK_TRAVEL_KEY_H
#define _UNIFIED_JOYSTICK_TRAVEL_KEY_H

#include "gpaddon.h"
#include "config.pb.h"
#include "GamepadEnums.h"

#define UnifiedJoystickTravelKeyName "Unified Joystick Travel Key"

struct TravelMappingEntry {
    uint32_t buttonMask;
    uint32_t dpadMask;
    uint16_t auxMask;
    uint8_t keyboardKeyBit;
    uint8_t mouseButtonMask;
    bool enabled;
};

class UnifiedJoystickTravelKeyAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void preprocess() {}
    virtual void process();
    virtual void postprocess(bool) {}
    virtual void reinit();
    virtual std::string name() { return UnifiedJoystickTravelKeyName; }

private:
    enum class StickSource : uint8_t {
        None = 0,
        ADS8332,
        MCP3208,
        OnboardADC,
    };

    void resolveSource();
    void buildMapsAndThresholds();
    bool readRawStick(uint8_t stickNum, uint16_t& rawX, uint16_t& rawY, uint16_t& centerX, uint16_t& centerY, uint16_t& adcMax);
    static void buildEntryFromMapping(TravelMappingEntry& entry, GpioAction action, uint32_t customButtonMask, uint32_t customDpadMask);
    static bool isEntryEffectivelyEmpty(const TravelMappingEntry& entry);
    static uint64_t getThresholdSquared(uint16_t adcMax, uint32_t thresholdPercent);
    void clearLastOutputs(class Gamepad* gamepad);
    void applyOutputs(class Gamepad* gamepad, const uint32_t currentButtons[2], const uint32_t currentDpad[2], const uint16_t currentAux[2], const uint64_t currentKeyboard[2], const uint8_t currentMouse[2]);

    TravelMappingEntry entries_[2];
    uint64_t thresholdSquared_[2] = {0, 0};
    StickSource source_ = StickSource::None;

    uint32_t lastButtons_[2] = {0, 0};
    uint32_t lastDpad_[2] = {0, 0};
    uint16_t lastAux_[2] = {0, 0};
    uint64_t lastKeyboard_[2] = {0, 0};
    uint8_t lastMouse_[2] = {0, 0};
};

#endif
