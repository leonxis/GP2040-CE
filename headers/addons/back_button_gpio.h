#ifndef _BACK_BUTTON_GPIO_H
#define _BACK_BUTTON_GPIO_H

#include "gpaddon.h"
#include "config.pb.h"
#include "addons/action_mapping_common.h"
#include "addons/back_button_divider.h"

#define BACK_BUTTON_GPIO_ADDON_NAME "背键GPIO映射"

// GPIO 直读背键插件：与分压背键插件共用背键映射选项与 KeyLayer 索引。
// 左背键1=GPIO15，右背键1=GPIO14，左背键2=GPIO22，右背键2=GPIO25，
// 耳机背键=GPIO23（映射存放于 leftElMapping）。
class BackButtonGpioAddon : public BackButtonDividerAddon {
public:
    bool available() override;
    void setup() override;
    void preprocess() override;
    void process() override;
    void reinit() override;
    std::string name() override { return BACK_BUTTON_GPIO_ADDON_NAME; }
    bool isIndexStable(uint8_t index) const override;

private:
    void buildMappings();
    void applySlotIfEnabled(Gamepad* gamepad, uint8_t index);

    ActionMappingCommon::ActionMappingTable mapTable_;
    ActionMappingCommon::ActionOutputScope outputScope_;
    ActionMappingCommon::DebounceBoolState debounce_[5];

    static constexpr uint8_t PIN_LEFT_BACK1 = 15;
    static constexpr uint8_t PIN_RIGHT_BACK1 = 14;
    static constexpr uint8_t PIN_LEFT_BACK2 = 22;
    static constexpr uint8_t PIN_RIGHT_BACK2 = 25;
    static constexpr uint8_t PIN_HEADPHONE = 23;
};

#endif
