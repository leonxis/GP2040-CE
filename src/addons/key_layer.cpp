#include "addons/key_layer.h"

#include "addons/back_button_divider.h"
#include "addons/two_key_touchpad.h"
#include "hml_back_mapping_preset.h"
#include "storagemanager.h"
#include "gamepad.h"

#include <cstdint>

bool KeyLayerAddon::available() {
    // 启用条件：映射层1(sets[1]) / 映射层2(sets[2]) 任一总闸开启；
    // 全部关闭（或固定槽未播种）则插件不加载
    const ProfileOptions& po = Storage::getInstance().getProfileOptions();
    if (po.gpioMappingsSets_count < 3) return false;
    return po.gpioMappingsSets[1].enabled || po.gpioMappingsSets[2].enabled;
}

void KeyLayerAddon::resetTransient() {
    for (uint8_t i = 0; i < activatorCount_; i++) {
        activators_[i].lastLevel = false;
        activators_[i].toggleOn = false;
    }
}

void KeyLayerAddon::buildCache() {
    Storage& storage = Storage::getInstance();

    // 1. 主控基础（functional）映射 + 主控激活器
    const GpioMappingInfo* functional = storage.getProfilePinMappings();
    activatorCount_ = 0;
    for (uint8_t pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
        ActionMappingCommon::parseActionMapping(functional[pin], baseEntry_[pin]);
        if (functional[pin].activatorMode != ACTIVATOR_OFF) {
            ActivatorCtx& a = activators_[activatorCount_++];
            a.src = ActivatorCtx::MAIN;
            a.index = pin;
            a.mode = functional[pin].activatorMode;
        }
    }

    // 2. 当前基础映射对应的层槽
    // 基础映射1 → sets[1]（映射层1）；基础映射2 → sets[2]（映射层2）
    const uint32_t idx = (storage.getConfig().gamepadOptions.profileNumber == 2) ? 2 : 1;
    const GpioMappings& layer = storage.getProfileOptions().gpioMappingsSets[idx];
    layerMasterOn_ = layer.enabled;
    for (uint8_t pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
        ActionMappingCommon::parseActionMapping(layer.pins[pin], layerEntry_[pin]);
    }

    // 3. 活动背键方案（6 背键 + 触摸左右键）+ 背键激活器
    const HmlBackMappingPreset& preset =
        getActiveHmlBackPreset(storage.getAddonOptions());
    const BackButtonAddonOptions& bb = preset.backButton;

    const GpioMappingInfo* backMappings[kBackKeyCount] = {
        &bb.leftElMapping, &bb.rightErMapping,
        &bb.leftBack1Mapping, &bb.rightBack1Mapping,
        &bb.leftBack2Mapping, &bb.rightBack2Mapping,
    };
    for (uint8_t i = 0; i < kBackKeyCount; i++) {
        const GpioMappingInfo& info = *backMappings[i];
        ActionMappingCommon::parseActionMapping(info, backEntry_[i]);
        if (info.activatorMode != ACTIVATOR_OFF) {
            ActivatorCtx& a = activators_[activatorCount_++];
            a.src = ActivatorCtx::BACK;
            a.index = i;
            a.mode = info.activatorMode;
        }
    }

    // 触摸左右键仅 parse（作为外部键在激活帧聚合其输出）；
    // 触摸键/使能键不具备激活器资格，不收集其 activatorMode
    ActionMappingCommon::parseActionMapping(preset.leftKeyMapping, touchEntry_[0]);
    ActionMappingCommon::parseActionMapping(preset.rightKeyMapping, touchEntry_[1]);
}

void KeyLayerAddon::setup() {
    buildCache();
    resetTransient();
    prevMasterEnabled_ = false;
}

void KeyLayerAddon::reinit() {
    buildCache();
    resetTransient();
    prevMasterEnabled_ = false;
}

void KeyLayerAddon::bindExternal(BackButtonDividerAddon* backDiv, TwoKeyTouchpadAddon* twoKey) {
    backDiv_ = backDiv;
    twoKey_ = twoKey;
}

void KeyLayerAddon::accumulate(MaskAcc& m,
                               const ActionMappingCommon::ActionMappingEntry& e) {
    m.buttons |= e.buttonMask;
    m.dpad |= e.dpadMask;
    m.aux |= e.auxMask;
    m.kbd |= e.keyboardMask;
    m.mouse |= e.mouseButtonMask;
    switch (e.complexAction) {
        case GpioAction::BUTTON_PRESS_MACRO_1: m.macro |= (1u << 0); break;
        case GpioAction::BUTTON_PRESS_MACRO_2: m.macro |= (1u << 1); break;
        case GpioAction::BUTTON_PRESS_MACRO_3: m.macro |= (1u << 2); break;
        case GpioAction::BUTTON_PRESS_MACRO_4: m.macro |= (1u << 3); break;
        case GpioAction::BUTTON_PRESS_MACRO_5: m.macro |= (1u << 4); break;
        case GpioAction::BUTTON_PRESS_MACRO_6: m.macro |= (1u << 5); break;
        default: break;
    }
}

void KeyLayerAddon::process() {
    // 所有逻辑在 preprocess() 完成
}

void KeyLayerAddon::preprocess() {
    Gamepad* gp = Storage::getInstance().GetGamepad();
    if (gp == nullptr) return;

    // 1) 求各激活器当前有效电平（主控=去抖电平；背键/触摸=插件实际输出态）
    bool levels[NUM_BANK0_GPIOS + kBackKeyCount];
    for (uint8_t i = 0; i < activatorCount_; i++) {
        const ActivatorCtx& a = activators_[i];
        bool level = false;
        switch (a.src) {
            case ActivatorCtx::MAIN:
                level = (gp->debouncedGpio >> a.index) & 1ULL;
                break;
            case ActivatorCtx::BACK:
                level = backDiv_ != nullptr && backDiv_->isIndexStable(a.index);
                break;
        }
        levels[i] = level;
    }

    // 2) 总闸边沿处理
    if (!prevMasterEnabled_ && layerMasterOn_) {
        // 上升沿：以当前电平为基准重同步，避免产生伪翻转
        for (uint8_t i = 0; i < activatorCount_; i++) {
            activators_[i].toggleOn = false;
            activators_[i].lastLevel = levels[i];
        }
    } else if (prevMasterEnabled_ && !layerMasterOn_) {
        // 下降沿：清全部瞬态
        resetTransient();
    }
    prevMasterEnabled_ = layerMasterOn_;

    // 3) TOGGLE 边沿翻转并汇总激活态
    bool anyOn = false;
    for (uint8_t i = 0; i < activatorCount_; i++) {
        ActivatorCtx& a = activators_[i];
        bool on;
        if (a.mode == ACTIVATOR_TOGGLE) {
            if (levels[i] && !a.lastLevel) a.toggleOn = !a.toggleOn;
            a.lastLevel = levels[i];
            on = a.toggleOn;
        } else {
            on = levels[i];
        }
        anyOn |= on;
    }

    // 4) 总闸关或无激活器：零动作（基础输出由 read()/外部插件各自保证）
    if (!layerMasterOn_ || !anyOn) return;

    // 5) 聚合外部键（背键/触摸）实际输出
    MaskAcc ext{};
    for (uint8_t i = 0; i < kBackKeyCount; i++) {
        if (backDiv_ != nullptr && backDiv_->isIndexStable(i)) {
            accumulate(ext, backEntry_[i]);
        }
    }
    if (twoKey_ != nullptr) {
        if (twoKey_->isSideActive(0)) accumulate(ext, touchEntry_[0]);
        if (twoKey_->isSideActive(1)) accumulate(ext, touchEntry_[1]);
    }

    // 6) 聚合主控：层槽该 pin 有映射（enabled）→ 层槽，否则 → 基础
    MaskAcc lay{};
    const Mask_t v = gp->debouncedGpio;
    for (uint8_t pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
        if ((v >> pin) & 1ULL) {
            const ActionMappingCommon::ActionMappingEntry& e =
                layerEntry_[pin].enabled ? layerEntry_[pin] : baseEntry_[pin];
            accumulate(lay, e);
        }
    }

    // 7) 一次性重写（不碰 lx/ly/rx/ry/lt/rt）
    gp->state.buttons = ext.buttons | lay.buttons;
    gp->state.dpad = ext.dpad | lay.dpad;
    gp->state.aux = ext.aux | lay.aux;
    gp->addonKeyboardKeyMask = ext.kbd | lay.kbd;
    gp->addonMouseButtonMask = ext.mouse | lay.mouse;
    gp->addonMacroTriggerMask = ext.macro | lay.macro;
}
