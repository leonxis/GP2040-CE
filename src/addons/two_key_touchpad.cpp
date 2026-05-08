#include "addons/two_key_touchpad.h"

#include "storagemanager.h"
#include "gamepad.h"
#include "hardware/gpio.h"
#include "helper.h"

#define TOUCHPAD_ENABLE_PIN_2KEY 12

// 阈值与主循环帧率相关；标记自 -1 起每帧递增（按住时）。
// marker1_ > OUTPUT_MIN：使能连续按住约从 -1→7 共 8 帧后才可能输出。
// markertouch_ > TOUCH_MIN：单侧或合并触摸连续低约从 -1→4 共 5 帧后才可能出触摸。
static constexpr int8_t TWO_KEY_MARKER_INITIAL = -1;
static constexpr int8_t TWO_KEY_MARKER1_OUTPUT_MIN = 35;    // 需 marker1_ > 本值
static constexpr int8_t TWO_KEY_MARKERTOUCH_TOUCH_MIN = 3; // 需 markertouch_ > 本值
static constexpr int8_t TWO_KEY_MARKER_SAT_MAX = 127;

enum TwoKeyMapIndex : uint8_t {
    LEFT_TOUCH = 0,
    RIGHT_TOUCH = 1,
    ENABLE_KEY = 2,
};

static int8_t bumpMarker(int8_t current) {
    return (current >= TWO_KEY_MARKER_SAT_MAX) ? TWO_KEY_MARKER_SAT_MAX
                                                : static_cast<int8_t>(current + 1);
}

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
    marker1_ = TWO_KEY_MARKER_INITIAL;
    markertouch_ = TWO_KEY_MARKER_INITIAL;
}

void TwoKeyTouchpadAddon::reinit() {
    buildMappings();
    outputScope_.reset();
    marker1_ = TWO_KEY_MARKER_INITIAL;
    markertouch_ = TWO_KEY_MARKER_INITIAL;
}

void TwoKeyTouchpadAddon::preprocess() {
    if (!isValidPin(pin_left) || !isValidPin(pin_right)) return;

    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }

    outputScope_.beginFrame(gamepad);

    const bool enablePressed = !gpio_get(TOUCHPAD_ENABLE_PIN_2KEY);
    const bool leftRaw  = !gpio_get((uint)pin_left);
    const bool rightRaw = !gpio_get((uint)pin_right);

    marker1_ = enablePressed ? bumpMarker(marker1_) : TWO_KEY_MARKER_INITIAL;
    const bool anyTouchRaw = leftRaw || rightRaw;
    markertouch_ = anyTouchRaw ? bumpMarker(markertouch_) : TWO_KEY_MARKER_INITIAL;

    if (marker1_ <= TWO_KEY_MARKER1_OUTPUT_MIN) {
        outputScope_.endFrame();
        return;
    }

    const ActionMappingCommon::ActionMappingEntry* enableEntry = mapTable_.at(ENABLE_KEY);

    if (markertouch_ > TWO_KEY_MARKERTOUCH_TOUCH_MIN) {
        if (enableEntry != nullptr) {
            ActionMappingCommon::clearActionMappingEntry(gamepad, *enableEntry);
        }
        if (leftRaw) {
            const ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(LEFT_TOUCH);
            if (entry != nullptr) {
                outputScope_.apply(gamepad, *entry);
            }
        }
        if (rightRaw) {
            const ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(RIGHT_TOUCH);
            if (entry != nullptr) {
                outputScope_.apply(gamepad, *entry);
            }
        }
    } else if (markertouch_ == TWO_KEY_MARKER_INITIAL && !leftRaw && !rightRaw
               && enableEntry != nullptr) {
        outputScope_.apply(gamepad, *enableEntry);
    }

    outputScope_.endFrame();
}

void TwoKeyTouchpadAddon::process() {
    // 所有逻辑在 preprocess() 完成
}
