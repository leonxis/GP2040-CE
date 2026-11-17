#include "addons/back_button_gpio.h"

#include "hml_back_mapping_preset.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "hardware/gpio.h"

namespace {
constexpr uint8_t BACK_GPIO_DEBOUNCE_FRAMES = 3u;

// 与 KeyLayer 背键索引一致：0=耳机背键(原 leftEl 槽)，1=保留，2=左B1，3=右B1，4=左B2，5=右B2
enum Slot : uint8_t {
    SLOT_HEADPHONE = 0,
    SLOT_RESERVED = 1,
    SLOT_LEFT_BACK1 = 2,
    SLOT_RIGHT_BACK1 = 3,
    SLOT_LEFT_BACK2 = 4,
    SLOT_RIGHT_BACK2 = 5,
};
} // namespace

bool BackButtonGpioAddon::available() {
    // 始终加载：是否输出由背键映射决定。
    return true;
}

void BackButtonGpioAddon::buildMappings() {
    const BackButtonAddonOptions& opts =
        getActiveBackButtonOptions(Storage::getInstance().getAddonOptions());
    mapTable_.setCount(6);
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(SLOT_HEADPHONE)) {
        ActionMappingCommon::parseActionMapping(opts.leftElMapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(SLOT_RESERVED)) {
        ActionMappingCommon::parseActionMapping(opts.rightErMapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(SLOT_LEFT_BACK1)) {
        ActionMappingCommon::parseActionMapping(opts.leftBack1Mapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(SLOT_RIGHT_BACK1)) {
        ActionMappingCommon::parseActionMapping(opts.rightBack1Mapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(SLOT_LEFT_BACK2)) {
        ActionMappingCommon::parseActionMapping(opts.leftBack2Mapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(SLOT_RIGHT_BACK2)) {
        ActionMappingCommon::parseActionMapping(opts.rightBack2Mapping, *entry);
    }
}

void BackButtonGpioAddon::applySlotIfEnabled(Gamepad* gamepad, uint8_t index) {
    const ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(index);
    if (entry != nullptr) {
        outputScope_.apply(gamepad, *entry);
    }
}

void BackButtonGpioAddon::setup() {
    const uint8_t pins[] = {
        PIN_LEFT_BACK1, PIN_RIGHT_BACK1, PIN_LEFT_BACK2, PIN_RIGHT_BACK2, PIN_HEADPHONE,
    };
    for (uint8_t pin : pins) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
    }
    buildMappings();
    outputScope_.reset();
    for (auto& state : debounce_) {
        ActionMappingCommon::resetDebounceBool(state, false);
    }
}

void BackButtonGpioAddon::reinit() {
    buildMappings();
    outputScope_.reset();
    for (auto& state : debounce_) {
        ActionMappingCommon::resetDebounceBool(state, false);
    }
}

void __not_in_flash_func(BackButtonGpioAddon::preprocess)() {
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }

    outputScope_.beginFrame(gamepad);

    const bool pressed[5] = {
        !gpio_get(PIN_HEADPHONE),
        !gpio_get(PIN_LEFT_BACK1),
        !gpio_get(PIN_RIGHT_BACK1),
        !gpio_get(PIN_LEFT_BACK2),
        !gpio_get(PIN_RIGHT_BACK2),
    };
    for (uint8_t i = 0; i < 5; i++) {
        ActionMappingCommon::updateDebounceBool(pressed[i], debounce_[i], BACK_GPIO_DEBOUNCE_FRAMES);
    }

    if (debounce_[0].stable) applySlotIfEnabled(gamepad, SLOT_HEADPHONE);
    if (debounce_[1].stable) applySlotIfEnabled(gamepad, SLOT_LEFT_BACK1);
    if (debounce_[2].stable) applySlotIfEnabled(gamepad, SLOT_RIGHT_BACK1);
    if (debounce_[3].stable) applySlotIfEnabled(gamepad, SLOT_LEFT_BACK2);
    if (debounce_[4].stable) applySlotIfEnabled(gamepad, SLOT_RIGHT_BACK2);

    outputScope_.endFrame();
}

void __not_in_flash_func(BackButtonGpioAddon::process)() {
    // 所有逻辑在 preprocess 中完成
}

bool BackButtonGpioAddon::isIndexStable(uint8_t index) const {
    switch (index) {
    case SLOT_HEADPHONE: return debounce_[0].stable;
    case SLOT_LEFT_BACK1: return debounce_[1].stable;
    case SLOT_RIGHT_BACK1: return debounce_[2].stable;
    case SLOT_LEFT_BACK2: return debounce_[3].stable;
    case SLOT_RIGHT_BACK2: return debounce_[4].stable;
    default: return false;
    }
}
