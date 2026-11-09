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
    void buildMapsAndThresholds();

    ActionMappingCommon::ActionMappingTable entries_;
    // Normalized travel thresholds (0..1), derived from the configured percent.
    float thresholds_[2] = {0.0f, 0.0f};
    ActionMappingCommon::ActionOutputScope outputScope_;
    ActionMappingCommon::DebounceBoolState debounceState_[2];
};

#endif
