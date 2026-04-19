#ifndef _BACK_BUTTON_DIVIDER_H
#define _BACK_BUTTON_DIVIDER_H

#include "gpaddon.h"
#include "BoardConfig.h"
#include "config.pb.h"
#include "addons/action_mapping_common.h"

#define BACK_BUTTON_DIVIDER_ADDON_NAME "背键分压映射"

class BackButtonDividerAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void preprocess();
    virtual void process();
    virtual void postprocess(bool) {}
    virtual std::string name() { return BACK_BUTTON_DIVIDER_ADDON_NAME; }
    virtual void reinit();
private:
    void buildMappings();
    ActionMappingCommon::ActionMappingTable mapTable_;
    ActionMappingCommon::ActionOutputScope outputScope_;

    bool adcInitialized = false;
    // 简单档位防抖：-1=无输出, 0=背键2, 1=背键1, 2=背键1+2
    ActionMappingCommon::DebounceLevelState leftDebounce_;
    ActionMappingCommon::DebounceLevelState rightDebounce_;
};

#endif

