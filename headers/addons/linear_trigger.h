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
    void reloadThresholds();  // 从 Storage 读取配置并重算 minAdc/maxAdc，仅 setup 时调用
    // 校准与死区/行程在 setup 中一次性算成阈值，preprocess 仅做整数线性映射
    int32_t minAdcL;
    int32_t maxAdcL;
    int32_t minAdcR;
    int32_t maxAdcR;
};

#endif // _LINEAR_TRIGGER_H
