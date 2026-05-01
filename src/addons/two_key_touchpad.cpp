#include "addons/two_key_touchpad.h"

#include "storagemanager.h"
#include "gamepad.h"
#include "hardware/gpio.h"
#include "helper.h"

#define TOUCHPAD_ENABLE_PIN_2KEY 12
// 连续多少帧一致才更新触摸键状态
static constexpr uint8_t TWO_KEY_TOUCH_DEBOUNCE_FRAMES = 3;

enum TwoKeyMapIndex : uint8_t {
    LEFT_TOUCH = 0,
    RIGHT_TOUCH = 1,
    ENABLE_KEY = 2,
};

bool TwoKeyTouchpadAddon::available() {
    const TwoKeyTouchpadOptions& opts = Storage::getInstance().getAddonOptions().twoKeyTouchpadOptions;
    if (!opts.enabled) return false;
    const PeripheralOptions& peri = Storage::getInstance().getPeripheralOptions();
    return isValidPin(peri.blockI2C1.sda) && isValidPin(peri.blockI2C1.scl);
}

void TwoKeyTouchpadAddon::buildMappings() {
    const TwoKeyTouchpadOptions& opts = Storage::getInstance().getAddonOptions().twoKeyTouchpadOptions;
    mapTable_.setCount(3);
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(LEFT_TOUCH)) {
        ActionMappingCommon::parseActionMapping(opts.leftKeyMapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(RIGHT_TOUCH)) {
        ActionMappingCommon::parseActionMapping(opts.rightKeyMapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(ENABLE_KEY)) {
        ActionMappingCommon::parseActionMapping(opts.enableKeyMapping, *entry);
    }
}

void TwoKeyTouchpadAddon::setup() {
    const PeripheralOptions& peri = Storage::getInstance().getPeripheralOptions();
    pin_left  = peri.blockI2C1.sda;   // SDA → 左触摸键
    pin_right = peri.blockI2C1.scl;   // SCL → 右触摸键

    if (!isValidPin(pin_left) || !isValidPin(pin_right)) return;

    // 左/右触摸键引脚：GPIO 输入 + 上拉（开关接地，低电平有效）
    gpio_init((uint)pin_left);
    gpio_set_dir((uint)pin_left, GPIO_IN);
    gpio_pull_up((uint)pin_left);

    gpio_init((uint)pin_right);
    gpio_set_dir((uint)pin_right, GPIO_IN);
    gpio_pull_up((uint)pin_right);

    // GPIO12 由正常 GPIO 映射系统初始化，此处确保上拉
    gpio_init(TOUCHPAD_ENABLE_PIN_2KEY);
    gpio_set_dir(TOUCHPAD_ENABLE_PIN_2KEY, GPIO_IN);
    gpio_pull_up(TOUCHPAD_ENABLE_PIN_2KEY);

    buildMappings();
    outputScope_.reset();
    ActionMappingCommon::resetDebounceBool(leftDebounce_, false);
    ActionMappingCommon::resetDebounceBool(rightDebounce_, false);
}

void TwoKeyTouchpadAddon::reinit() {
    buildMappings();
    outputScope_.reset();
    ActionMappingCommon::resetDebounceBool(leftDebounce_, false);
    ActionMappingCommon::resetDebounceBool(rightDebounce_, false);
}

void TwoKeyTouchpadAddon::preprocess() {
    if (!isValidPin(pin_left) || !isValidPin(pin_right)) return;

    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }

    outputScope_.beginFrame(gamepad);

    // ② 使能键（GPIO12）低有效，无防抖：未按下时本插件不输出触摸/直通逻辑。
    // 正常配置下 GPIO12 已被标记为 ASSIGNED_TO_ADDON，不会由 gamepad->read() 直接产生命令。
    const bool enablePressed = !gpio_get(TOUCHPAD_ENABLE_PIN_2KEY);
    if (!enablePressed) {
        outputScope_.endFrame();
        return;
    }

    const bool leftRaw  = !gpio_get((uint)pin_left);
    const bool rightRaw = !gpio_get((uint)pin_right);
    ActionMappingCommon::updateDebounceBool(leftRaw, leftDebounce_, TWO_KEY_TOUCH_DEBOUNCE_FRAMES);
    ActionMappingCommon::updateDebounceBool(rightRaw, rightDebounce_, TWO_KEY_TOUCH_DEBOUNCE_FRAMES);

    const bool leftPressed = leftDebounce_.stable;
    const bool rightPressed = rightDebounce_.stable;
    const ActionMappingCommon::ActionMappingEntry* enableEntry = mapTable_.at(ENABLE_KEY);

    if (leftPressed || rightPressed) {
        // 触摸键模式：优先输出左/右触摸映射。
        // 这里保留对 enableEntry 的清理作为兼容性兜底（异常配置/历史配置未标记 ASSIGNED_TO_ADDON 时避免残留）。
        if (enableEntry != nullptr) {
            ActionMappingCommon::clearActionMappingEntry(gamepad, *enableEntry);
        }
        if (leftPressed) {
            const ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(LEFT_TOUCH);
            if (entry != nullptr) {
                outputScope_.apply(gamepad, *entry);
            }
        }
        if (rightPressed) {
            const ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(RIGHT_TOUCH);
            if (entry != nullptr) {
                outputScope_.apply(gamepad, *entry);
            }
        }
    } else if (enableEntry != nullptr && !leftRaw && !rightRaw) {
        // 无触摸（含原始电平）：输出使能键映射。
        // 若任一触摸键 raw 已按下但防抖尚未稳定，暂不直通 enable，避免“使能键闪现”。
        outputScope_.apply(gamepad, *enableEntry);
    }

    outputScope_.endFrame();
}

void TwoKeyTouchpadAddon::process() {
    // 所有逻辑在 preprocess() 完成
}
