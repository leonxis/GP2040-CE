#ifndef _TWO_KEY_TOUCHPAD_H
#define _TWO_KEY_TOUCHPAD_H

#include "gpaddon.h"
#include "BoardConfig.h"
#include "config.pb.h"
#include "addons/action_mapping_common.h"

#define TWO_KEY_TOUCHPAD_ADDON_NAME "2键触摸板"

// 2键触摸板：I2C1 SDA = 左触摸键引脚，I2C1 SCL = 右触摸键引脚（开关接地，低有效）
// GPIO12 = 使能键（低有效）
// 逻辑：
//   GPIO12 按下 + 左/右引脚低 → 输出左/右触摸键映射，抑制 GPIO12 原映射
//   GPIO12 按下 + 两引脚均高 → 仅输出 GPIO12 原映射（直通）
//   GPIO12 松开 → 全部清除
class TwoKeyTouchpadAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void process();
    virtual void preprocess();
    virtual void postprocess(bool) {}
    virtual std::string name() { return TWO_KEY_TOUCHPAD_ADDON_NAME; }
    virtual void reinit();
private:
    void buildMappings();

    int32_t pin_left  = -1;   // I2C1 SDA → 左触摸键
    int32_t pin_right = -1;   // I2C1 SCL → 右触摸键
    ActionMappingCommon::ActionMappingTable mapTable_;
    ActionMappingCommon::ActionOutputScope outputScope_;

    // 触摸键防抖：连续若干帧稳定再更新状态（GPIO12 仍保持无防抖，避免引入新的延迟）
    ActionMappingCommon::DebounceBoolState leftDebounce_;
    ActionMappingCommon::DebounceBoolState rightDebounce_;
};

#endif
