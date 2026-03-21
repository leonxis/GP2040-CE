#ifndef _TWO_KEY_TOUCHPAD_H
#define _TWO_KEY_TOUCHPAD_H

#include "gpaddon.h"
#include "BoardConfig.h"
#include "config.pb.h"

#define TWO_KEY_TOUCHPAD_ADDON_NAME "2键触摸板"

// 预解析的快速映射：运行时 OR mask；仅 ANALOG/MENU/KEYBOARD 标为 isComplex
struct TwoKeyFastMapping {
    uint32_t buttonMask;
    uint32_t dpadMask;
    uint32_t auxMask;
    bool isComplex;
    const GpioMappingInfo* originalMapping;
};

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
    TwoKeyFastMapping leftMapping;
    TwoKeyFastMapping rightMapping;
    TwoKeyFastMapping touchpadMapping;   // GPIO12 当前映射键
    GpioMappingInfo   touchpadMappingInfo;  // 持有拷贝供指针引用

    // 触摸键防抖：连续若干帧稳定再更新状态（GPIO12 仍保持无防抖，避免引入新的延迟）
    bool leftStablePressed  = false;
    bool leftPendingPressed = false;
    uint8_t leftDebounceCount = 0;
    bool rightStablePressed  = false;
    bool rightPendingPressed = false;
    uint8_t rightDebounceCount = 0;

    // 仅撤销上一帧本插件实际写入的输出（左/右触摸或 GPIO12 直通），避免按配置全量 mask 每帧清空误伤 GPIO
    uint32_t last_out_buttons_ = 0;
    uint32_t last_out_dpad_    = 0;
    uint32_t last_out_aux_     = 0;
    const TwoKeyFastMapping* last_applied_complex_[2] = {};
    uint8_t last_applied_complex_count_ = 0;
};

#endif
