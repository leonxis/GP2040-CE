#ifndef _KEY_LAYER_H_
#define _KEY_LAYER_H_

#include "gpaddon.h"
#include "BoardConfig.h"
#include "config.pb.h"
#include "addons/action_mapping_common.h"

#include <cstdint>

#define KEY_LAYER_ADDON_NAME "按键映射层"

class HmlBackKeyAddon;
class TwoKeyTouchpadAddon;

// 按键映射层插件：
// 当前基础映射（1 或 2）对应的层槽（sets[1]/sets[2]）总闸开启，
// 且任一激活器有效（HOLD 按住 / TOGGLE 翻转 ON）时，对主控 48 引脚输出做
// 一次性重写：层槽该 pin 非 NONE → 层槽映射，NONE → 基础映射。
// 背键/触摸键不参与切换，其输出与层状态无关（仅可作为激活器来源）。
class KeyLayerAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void process();
    virtual void preprocess();
    virtual void postprocess(bool) {}
    virtual std::string name() { return KEY_LAYER_ADDON_NAME; }
    virtual void reinit();

    // 由 gp2040.cpp 在插件注册后绑定（未加载的插件为 nullptr）
    void bindExternal(HmlBackKeyAddon* hmlBack, TwoKeyTouchpadAddon* twoKey);

private:
    struct MaskAcc {
        uint32_t buttons = 0;
        uint8_t dpad = 0;
        uint16_t aux = 0;
        uint64_t kbd = 0;
        uint8_t mouse = 0;
        uint8_t macro = 0;
    };

    struct ActivatorCtx {
        // 仅主控引脚与背键可作激活器；触摸左/右键与使能键不具备激活器资格
        enum Src : uint8_t { MAIN, BACK } src;
        uint8_t index = 0;            // MAIN=pin, BACK=背键序号
        ActivatorMode mode = ACTIVATOR_OFF;
        bool lastLevel = false;
        bool toggleOn = false;
    };

    void buildCache();
    void resetTransient();
    static void accumulate(MaskAcc& m, const ActionMappingCommon::ActionMappingEntry& e);

    ActionMappingCommon::ActionMappingEntry baseEntry_[NUM_BANK0_GPIOS];
    ActionMappingCommon::ActionMappingEntry layerEntry_[NUM_BANK0_GPIOS];
    bool layerMasterOn_ = false;
    ActionMappingCommon::ActionMappingEntry backEntry_[10];
    ActionMappingCommon::ActionMappingEntry touchEntry_[2];
    ActivatorCtx activators_[NUM_BANK0_GPIOS + 10];
    uint8_t activatorCount_ = 0;

    bool prevMasterEnabled_ = false;
    HmlBackKeyAddon* hmlBack_ = nullptr;
    TwoKeyTouchpadAddon* twoKey_ = nullptr;
};

#endif
