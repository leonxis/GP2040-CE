#ifndef _HML_BACK_KEY_H
#define _HML_BACK_KEY_H

#include "gpaddon.h"
#include "BoardConfig.h"
#include "config.pb.h"
#include "addons/action_mapping_common.h"

#include <cstdint>

#define HML_BACK_KEY_ADDON_NAME "背键映射"

// HML 数字背键插件：读取 BoardConfig 中 HML_BACK_KEY_*_PIN 定义的 10 个背键/FN/MT
// GPIO（低电平有效，输入上拉），映射来自 HmlBackMappingPreset 当前激活方案的
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
    ActionMappingCommon::DebounceBoolState debounce_[10];
};

#endif
