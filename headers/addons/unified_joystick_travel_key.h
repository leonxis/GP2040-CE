#ifndef _UNIFIED_JOYSTICK_TRAVEL_KEY_H
#define _UNIFIED_JOYSTICK_TRAVEL_KEY_H

#include "gpaddon.h"
#include "config.pb.h"
#include "GamepadEnums.h"
#include "addons/action_mapping_common.h"

#define UnifiedJoystickTravelKeyName "Unified Joystick Travel Key"

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
        OnboardADC,
    };

    void resolveSource();
    void buildMapsAndThresholds();
    bool readRawStick(uint8_t stickNum, uint16_t& rawX, uint16_t& rawY, uint16_t& centerX, uint16_t& centerY, uint16_t& adcMax);
    static uint64_t getThresholdSquared(uint16_t adcMax, uint32_t thresholdPercent);

    ActionMappingCommon::ActionMappingTable entries_;
    uint64_t thresholdSquared_[2] = {0, 0};
    StickSource source_ = StickSource::None;
    ActionMappingCommon::ActionOutputScope outputScope_;
    ActionMappingCommon::DebounceBoolState debounceState_[2];
};

#endif
