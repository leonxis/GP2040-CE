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
    virtual void reinit() {}  // 线性扳机校准为全局配置，不随 profile 切换，无需在 reinit 中重载
private:
    void reloadThresholds();
    bool invertL = false;
    bool invertR = false;
    // Decreasing: idleAdc=松开侧(高 ADC), pressAdc=按到底侧(低 ADC)
    // Increasing: idleAdc=松开侧(低 ADC), pressAdc=按到底侧(高 ADC)
    int32_t idleAdcL = 0;
    int32_t pressAdcL = 0;
    int32_t idleAdcR = 0;
    int32_t pressAdcR = 0;
};

#endif // _LINEAR_TRIGGER_H
