#ifndef _HML_BACK_KEY_H
#define _HML_BACK_KEY_H

#include "gpaddon.h"
#include "BoardConfig.h"
#include "config.pb.h"
#include "addons/action_mapping_common.h"
#include "hardware/gpio.h"

#include <cstdint>

#define HML_BACK_KEY_ADDON_NAME "背键映射"

// 仅 48 GPIO 的 RP2350B 版型具备完整的背键/FN/MT 独立引脚
#define HML_BACK_KEY_SUPPORTED (NUM_BANK0_GPIOS >= 48)
#define HML_BACK_KEY_COUNT 10

// 背键硬件定义：引脚号在插件内硬编码（2354B 版型固定走线），
// 不再从 BoardConfig 读取；fieldName 与网页 get/setTwoKeyTouchpadOptions
// 接口 backKeys 字段名保持一致。
struct HmlBackKeyDef {
    uint8_t pin;
    const char* fieldName;
    GpioMappingInfo HmlBackMappingPreset::*presetField;
};

extern const HmlBackKeyDef kHmlBackKeyDefs[HML_BACK_KEY_COUNT];

// HML 数字背键插件：读取硬编码的 10 个背键/FN/MT GPIO（低电平有效，输入上拉），
// 映射来自 HmlBackMappingPreset 当前激活方案的
// 左/右背键1-3 + 左/右FN + 左/右MT 字段，输出走 ActionMappingCommon 统一映射。
class HmlBackKeyAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void process();
    virtual void preprocess();
    virtual void postprocess(bool) {}
    virtual std::string name() { return HML_BACK_KEY_ADDON_NAME; }
    virtual void reinit();
private:
    void buildMappings();

    ActionMappingCommon::ActionMappingTable mapTable_;
    ActionMappingCommon::ActionOutputScope outputScope_;
    ActionMappingCommon::DebounceBoolState debounce_[HML_BACK_KEY_COUNT];
};

#endif
