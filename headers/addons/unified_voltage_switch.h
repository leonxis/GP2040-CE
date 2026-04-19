#ifndef _UNIFIED_VOLTAGE_SWITCH_H
#define _UNIFIED_VOLTAGE_SWITCH_H

#include "gpaddon.h"
#include "GamepadEnums.h"
#include "config.pb.h"
#include "addons/action_mapping_common.h"

#define UnifiedVoltageSwitchName "Unified Voltage Switch"

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
    static uint16_t scaledThreshold(float ratio, uint16_t adcMax);

    ActionMappingCommon::ActionMappingTable left_map_;
    ActionMappingCommon::ActionMappingTable right_map_;
    ActionMappingCommon::DebounceLevelState leftDebounce_;
    ActionMappingCommon::DebounceLevelState rightDebounce_;
    ActionMappingCommon::ActionOutputScope outputScope_;
};

#endif
