#ifndef _LINEAR_TRIGGER_H
#define _LINEAR_TRIGGER_H

#include "gpaddon.h"
#include "BoardConfig.h"

#ifndef LINEAR_TRIGGER_ENABLED
#define LINEAR_TRIGGER_ENABLED 0
#endif

#define LINEAR_TRIGGER_ADDON_NAME "Linear Trigger"

// Fixed ADC pins: GPIO28 = R2, GPIO29 = L2 (ADC channels 2 and 3)
#define LINEAR_R2_PIN 28
#define LINEAR_L2_PIN 29

class LinearTriggerAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void process() {}
    virtual void preprocess();
    virtual void postprocess(bool) {}
    virtual std::string name() { return LINEAR_TRIGGER_ADDON_NAME; }
    virtual void reinit() {}
private:
    uint32_t deadzoneL;
    uint32_t deadzoneR;
    uint32_t travelL;
    uint32_t travelR;
};

#endif // _LINEAR_TRIGGER_H
