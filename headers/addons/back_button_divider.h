#ifndef _BACK_BUTTON_DIVIDER_H
#define _BACK_BUTTON_DIVIDER_H

#include "gpaddon.h"
#include "BoardConfig.h"
#include "config.pb.h"

#define BACK_BUTTON_DIVIDER_ADDON_NAME "背键分压映射"

struct BackFastMapping {
    uint32_t buttonMask;
    uint32_t dpadMask;
    uint32_t auxMask;
    bool isComplex;
    const GpioMappingInfo* originalMapping;
};

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
    void applyMapping(Gamepad* gamepad, const BackFastMapping& m);

    BackFastMapping leftBack1;
    BackFastMapping leftBack2;
    BackFastMapping rightBack1;
    BackFastMapping rightBack2;

    bool adcInitialized = false;
    // 简单档位防抖：-1=无输出, 0=背键2, 1=背键1, 2=背键1+2
    int8_t leftStableLevel  = -1;
    int8_t leftPendingLevel = -1;
    uint8_t leftDebounceCount = 0;
    int8_t rightStableLevel  = -1;
    int8_t rightPendingLevel = -1;
    uint8_t rightDebounceCount = 0;

    // 仅撤销上一帧本插件实际写入的输出，避免按「配置可能占用的位」每帧清空导致误伤 GPIO 等其它来源
    uint32_t last_out_buttons_ = 0;
    uint32_t last_out_dpad_    = 0;
    uint32_t last_out_aux_     = 0;
    const BackFastMapping* last_applied_[4] = {};
    uint8_t last_applied_count_ = 0;
};

#endif

