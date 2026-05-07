#ifndef _TWO_KEY_TOUCHPAD_H
#define _TWO_KEY_TOUCHPAD_H

#include "gpaddon.h"
#include "BoardConfig.h"
#include "config.pb.h"
#include "addons/action_mapping_common.h"

#include <cstdint>

#define TWO_KEY_TOUCHPAD_ADDON_NAME "2键触摸板"

// 2键触摸板：I2C1 SDA = 左触摸键引脚，I2C1 SCL = 右触摸键引脚（开关接地，低有效）
// GPIO12 = 使能键（低有效）
// 逻辑（双计数器门控，每帧更新；左右触摸合并为一个触摸计数 markertouch_）：
//   标记1（marker1_）：使能未按下置 -1；按下则每帧 +1（自 -1 递增，带饱和）。仅当标记1 > 6 时本插件才可能输出。
//   标记2：任一侧触摸 raw 低则每帧 +1，否则置 -1（合并计数，带饱和）。
//   标记1 > 6 且标记2 > 3：按左/右 raw 输出触摸映射，并抑制使能映射。
//   标记1 > 6 且标记2 == -1 且两路 raw 均为高：输出使能键映射（双 raw 高防止触点毛刺误出使能）。
//   标记2 ∈ {0,1,2,3}：死区，本帧无输出。
//   使能未按下时仍每帧更新标记2并 begin/endFrame，但不 apply；标记1 置 -1。
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

    int8_t marker1_     = -1;   // 使能键标记：未按下时固定 -1，按下时每帧 +1（饱和）
    int8_t markertouch_ = -1;   // 触摸合并标记：无触摸时固定 -1，任一侧按下时每帧 +1（饱和）
};

#endif
