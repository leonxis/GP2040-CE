#include "addons/two_key_touchpad.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "gamepad/GamepadState.h"
#include "config.pb.h"
#include "hardware/gpio.h"
#include "helper.h"
#include "pico/time.h"
#include "eventmanager.h"

#define TOUCHPAD_ENABLE_PIN_2KEY 12
static constexpr uint32_t KEYBOARD_KEY_ACTION_BASE_2KEY = 131;
static constexpr uint8_t ADDON_MOUSE_LEFT_BIT = (1u << 0);
static constexpr uint8_t ADDON_MOUSE_RIGHT_BIT = (1u << 1);
static constexpr uint8_t ADDON_MOUSE_MIDDLE_BIT = (1u << 2);
// 连续多少帧一致才更新触摸键状态
static constexpr uint8_t TWO_KEY_TOUCH_DEBOUNCE_FRAMES = 3;

// ──────────────────────────────────────────────────────────────────────────────
// 静态辅助：将 GpioMappingInfo 解析为 TwoKeyFastMapping
// ──────────────────────────────────────────────────────────────────────────────
static void parseMapping2Key(const GpioMappingInfo& src, TwoKeyFastMapping& dst) {
    dst.buttonMask = 0;
    dst.dpadMask   = 0;
    dst.auxMask    = 0;
    dst.isComplex  = false;
    dst.originalMapping = &src;

    if (src.action == GpioAction::NONE) return;

    if (src.action == GpioAction::CUSTOM_BUTTON_COMBO) {
        dst.buttonMask = src.customButtonMask;
        if (src.customDpadMask & GAMEPAD_MASK_DU) dst.dpadMask |= GAMEPAD_MASK_UP;
        if (src.customDpadMask & GAMEPAD_MASK_DD) dst.dpadMask |= GAMEPAD_MASK_DOWN;
        if (src.customDpadMask & GAMEPAD_MASK_DL) dst.dpadMask |= GAMEPAD_MASK_LEFT;
        if (src.customDpadMask & GAMEPAD_MASK_DR) dst.dpadMask |= GAMEPAD_MASK_RIGHT;
        return;
    }

    switch (src.action) {
        case GpioAction::BUTTON_PRESS_UP:    dst.dpadMask   |= GAMEPAD_MASK_UP;    break;
        case GpioAction::BUTTON_PRESS_DOWN:  dst.dpadMask   |= GAMEPAD_MASK_DOWN;  break;
        case GpioAction::BUTTON_PRESS_LEFT:  dst.dpadMask   |= GAMEPAD_MASK_LEFT;  break;
        case GpioAction::BUTTON_PRESS_RIGHT: dst.dpadMask   |= GAMEPAD_MASK_RIGHT; break;
        case GpioAction::BUTTON_PRESS_B1:    dst.buttonMask |= GAMEPAD_MASK_B1;    break;
        case GpioAction::BUTTON_PRESS_B2:    dst.buttonMask |= GAMEPAD_MASK_B2;    break;
        case GpioAction::BUTTON_PRESS_B3:    dst.buttonMask |= GAMEPAD_MASK_B3;    break;
        case GpioAction::BUTTON_PRESS_B4:    dst.buttonMask |= GAMEPAD_MASK_B4;    break;
        case GpioAction::BUTTON_PRESS_L1:    dst.buttonMask |= GAMEPAD_MASK_L1;    break;
        case GpioAction::BUTTON_PRESS_R1:    dst.buttonMask |= GAMEPAD_MASK_R1;    break;
        case GpioAction::BUTTON_PRESS_L2:    dst.buttonMask |= GAMEPAD_MASK_L2;    break;
        case GpioAction::BUTTON_PRESS_R2:    dst.buttonMask |= GAMEPAD_MASK_R2;    break;
        case GpioAction::BUTTON_PRESS_S1:    dst.buttonMask |= GAMEPAD_MASK_S1;    break;
        case GpioAction::BUTTON_PRESS_S2:    dst.buttonMask |= GAMEPAD_MASK_S2;    break;
        case GpioAction::BUTTON_PRESS_L3:    dst.buttonMask |= GAMEPAD_MASK_L3;    break;
        case GpioAction::BUTTON_PRESS_R3:    dst.buttonMask |= GAMEPAD_MASK_R3;    break;
        case GpioAction::BUTTON_PRESS_A1:    dst.buttonMask |= GAMEPAD_MASK_A1;    break;
        case GpioAction::BUTTON_PRESS_A2:    dst.buttonMask |= GAMEPAD_MASK_A2;    break;
        case GpioAction::BUTTON_PRESS_A3:    dst.buttonMask |= GAMEPAD_MASK_A3;    break;
        case GpioAction::BUTTON_PRESS_A4:    dst.buttonMask |= GAMEPAD_MASK_A4;    break;
        case GpioAction::BUTTON_PRESS_E1:    dst.buttonMask |= GAMEPAD_MASK_E1;    break;
        case GpioAction::BUTTON_PRESS_E2:    dst.buttonMask |= GAMEPAD_MASK_E2;    break;
        case GpioAction::BUTTON_PRESS_E3:    dst.buttonMask |= GAMEPAD_MASK_E3;    break;
        case GpioAction::BUTTON_PRESS_E4:    dst.buttonMask |= GAMEPAD_MASK_E4;    break;
        case GpioAction::BUTTON_PRESS_E5:    dst.buttonMask |= GAMEPAD_MASK_E5;    break;
        case GpioAction::BUTTON_PRESS_E6:    dst.buttonMask |= GAMEPAD_MASK_E6;    break;
        case GpioAction::BUTTON_PRESS_E7:    dst.buttonMask |= GAMEPAD_MASK_E7;    break;
        case GpioAction::BUTTON_PRESS_E8:    dst.buttonMask |= GAMEPAD_MASK_E8;    break;
        case GpioAction::BUTTON_PRESS_E9:    dst.buttonMask |= GAMEPAD_MASK_E9;    break;
        case GpioAction::BUTTON_PRESS_E10:   dst.buttonMask |= GAMEPAD_MASK_E10;   break;
        case GpioAction::BUTTON_PRESS_E11:   dst.buttonMask |= GAMEPAD_MASK_E11;   break;
        case GpioAction::BUTTON_PRESS_E12:   dst.buttonMask |= GAMEPAD_MASK_E12;   break;
        case GpioAction::BUTTON_PRESS_FN:    dst.auxMask    |= AUX_MASK_FUNCTION;  break;
        default:
            dst.isComplex = true;
            break;
    }
}

static void applyComplexMapping2Key(Gamepad* gamepad, const GpioMappingInfo& m) {
    switch (m.action) {
        case GpioAction::ANALOG_DIRECTION_LS_X_NEG: gamepad->state.lx = GAMEPAD_JOYSTICK_MIN; break;
        case GpioAction::ANALOG_DIRECTION_LS_X_POS: gamepad->state.lx = GAMEPAD_JOYSTICK_MAX; break;
        case GpioAction::ANALOG_DIRECTION_LS_Y_NEG: gamepad->state.ly = GAMEPAD_JOYSTICK_MIN; break;
        case GpioAction::ANALOG_DIRECTION_LS_Y_POS: gamepad->state.ly = GAMEPAD_JOYSTICK_MAX; break;
        case GpioAction::ANALOG_DIRECTION_RS_X_NEG: gamepad->state.rx = GAMEPAD_JOYSTICK_MIN; break;
        case GpioAction::ANALOG_DIRECTION_RS_X_POS: gamepad->state.rx = GAMEPAD_JOYSTICK_MAX; break;
        case GpioAction::ANALOG_DIRECTION_RS_Y_NEG: gamepad->state.ry = GAMEPAD_JOYSTICK_MIN; break;
        case GpioAction::ANALOG_DIRECTION_RS_Y_POS: gamepad->state.ry = GAMEPAD_JOYSTICK_MAX; break;
        case GpioAction::MENU_NAVIGATION_UP:     EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_UP)); break;
        case GpioAction::MENU_NAVIGATION_DOWN:   EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_DOWN)); break;
        case GpioAction::MENU_NAVIGATION_LEFT:   EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_LEFT)); break;
        case GpioAction::MENU_NAVIGATION_RIGHT:  EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_RIGHT)); break;
        case GpioAction::MENU_NAVIGATION_SELECT: EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_SELECT)); break;
        case GpioAction::MENU_NAVIGATION_BACK:   EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_BACK)); break;
        case GpioAction::MENU_NAVIGATION_TOGGLE: EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_TOGGLE)); break;
        default:
            if (m.action >= GpioAction::KEYBOARD_KEY_A && m.action <= GpioAction::KEYBOARD_KEY_9) {
                gamepad->addonKeyboardKeyMask |= (1ULL << (static_cast<uint32_t>(m.action) - KEYBOARD_KEY_ACTION_BASE_2KEY));
            } else if (m.action == GpioAction::MOUSE_LEFT_BUTTON) {
                gamepad->addonMouseButtonMask |= ADDON_MOUSE_LEFT_BIT;
            } else if (m.action == GpioAction::MOUSE_RIGHT_BUTTON) {
                gamepad->addonMouseButtonMask |= ADDON_MOUSE_RIGHT_BIT;
            } else if (m.action == GpioAction::MOUSE_MIDDLE_BUTTON) {
                gamepad->addonMouseButtonMask |= ADDON_MOUSE_MIDDLE_BIT;
            }
            break;
    }
}

static void clearComplexMapping2Key(Gamepad* gamepad, const GpioMappingInfo& m) {
    switch (m.action) {
        case GpioAction::ANALOG_DIRECTION_LS_X_NEG:
        case GpioAction::ANALOG_DIRECTION_LS_X_POS:
            gamepad->state.lx = GAMEPAD_JOYSTICK_MID; break;
        case GpioAction::ANALOG_DIRECTION_LS_Y_NEG:
        case GpioAction::ANALOG_DIRECTION_LS_Y_POS:
            gamepad->state.ly = GAMEPAD_JOYSTICK_MID; break;
        case GpioAction::ANALOG_DIRECTION_RS_X_NEG:
        case GpioAction::ANALOG_DIRECTION_RS_X_POS:
            gamepad->state.rx = GAMEPAD_JOYSTICK_MID; break;
        case GpioAction::ANALOG_DIRECTION_RS_Y_NEG:
        case GpioAction::ANALOG_DIRECTION_RS_Y_POS:
            gamepad->state.ry = GAMEPAD_JOYSTICK_MID; break;
        case GpioAction::MENU_NAVIGATION_UP:
        case GpioAction::MENU_NAVIGATION_DOWN:
        case GpioAction::MENU_NAVIGATION_LEFT:
        case GpioAction::MENU_NAVIGATION_RIGHT:
        case GpioAction::MENU_NAVIGATION_SELECT:
        case GpioAction::MENU_NAVIGATION_BACK:
        case GpioAction::MENU_NAVIGATION_TOGGLE:
            break;
        default:
            if (m.action >= GpioAction::KEYBOARD_KEY_A && m.action <= GpioAction::KEYBOARD_KEY_9) {
                gamepad->addonKeyboardKeyMask &= ~(1ULL << (static_cast<uint32_t>(m.action) - KEYBOARD_KEY_ACTION_BASE_2KEY));
            } else if (m.action == GpioAction::MOUSE_LEFT_BUTTON) {
                gamepad->addonMouseButtonMask &= ~ADDON_MOUSE_LEFT_BIT;
            } else if (m.action == GpioAction::MOUSE_RIGHT_BUTTON) {
                gamepad->addonMouseButtonMask &= ~ADDON_MOUSE_RIGHT_BIT;
            } else if (m.action == GpioAction::MOUSE_MIDDLE_BUTTON) {
                gamepad->addonMouseButtonMask &= ~ADDON_MOUSE_MIDDLE_BIT;
            }
            break;
    }
}

// ──────────────────────────────────────────────────────────────────────────────

bool TwoKeyTouchpadAddon::available() {
    const TwoKeyTouchpadOptions& opts = Storage::getInstance().getAddonOptions().twoKeyTouchpadOptions;
    if (!opts.enabled) return false;
    const PeripheralOptions& peri = Storage::getInstance().getPeripheralOptions();
    return isValidPin(peri.blockI2C1.sda) && isValidPin(peri.blockI2C1.scl);
}

void TwoKeyTouchpadAddon::buildMappings() {
    const TwoKeyTouchpadOptions& opts = Storage::getInstance().getAddonOptions().twoKeyTouchpadOptions;
    parseMapping2Key(opts.leftKeyMapping,  leftMapping);
    parseMapping2Key(opts.rightKeyMapping, rightMapping);

    // 读取 GPIO12 在背键映射中配置的动作（触摸板映射键）
    // 使用 functionalPinMappings（经 profile 合并后的有效映射）
    const GpioMappingInfo* pins = Storage::getInstance().getProfilePinMappings();
    touchpadMappingInfo = pins[TOUCHPAD_ENABLE_PIN_2KEY];
    parseMapping2Key(touchpadMappingInfo, touchpadMapping);
    touchpadMapping.originalMapping = &touchpadMappingInfo;
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
}

void TwoKeyTouchpadAddon::reinit() {
    last_out_buttons_ = last_out_dpad_ = last_out_aux_ = 0;
    last_applied_complex_count_ = 0;
    buildMappings();
}

void TwoKeyTouchpadAddon::preprocess() {
    if (!isValidPin(pin_left) || !isValidPin(pin_right)) return;

    Gamepad* gamepad = Storage::getInstance().GetGamepad();

    // ① 只清除上一帧本插件实际写入的输出（与背键分压/MCP3208 CH2/CH5 思路一致）
    gamepad->state.buttons &= ~last_out_buttons_;
    gamepad->state.dpad    &= ~last_out_dpad_;
    gamepad->state.aux     &= ~last_out_aux_;
    for (uint8_t i = 0; i < last_applied_complex_count_; i++) {
        const TwoKeyFastMapping* p = last_applied_complex_[i];
        if (p && p->isComplex && p->originalMapping)
            clearComplexMapping2Key(gamepad, *p->originalMapping);
    }
    last_out_buttons_ = last_out_dpad_ = last_out_aux_ = 0;
    last_applied_complex_count_ = 0;

    auto recordApplied = [&](const TwoKeyFastMapping& m) {
        last_out_buttons_ |= m.buttonMask;
        last_out_dpad_    |= m.dpadMask;
        last_out_aux_     |= m.auxMask;
        if (m.isComplex && last_applied_complex_count_ < 2)
            last_applied_complex_[last_applied_complex_count_++] = &m;
    };

    auto applyMappingAndRecord = [&](const TwoKeyFastMapping& m) {
        gamepad->state.buttons |= m.buttonMask;
        gamepad->state.dpad    |= m.dpadMask;
        gamepad->state.aux     |= m.auxMask;
        if (m.isComplex)
            applyComplexMapping2Key(gamepad, *m.originalMapping);
        recordApplied(m);
    };

    // ② 使能键（GPIO12）低有效，无防抖：未按下时不输出触摸/直通逻辑，GPIO12 由 gamepad->read() 决定
    bool enablePressed = !gpio_get(TOUCHPAD_ENABLE_PIN_2KEY);

    // ③ 根据使能键状态决定输出
    if (!enablePressed) return;  // GPIO12 未按下：本插件不叠加输出，结束

    bool leftRaw  = !gpio_get((uint)pin_left);
    bool rightRaw = !gpio_get((uint)pin_right);

    auto updateDebounceBool = [](bool raw, bool& stable, bool& pending, uint8_t& count) {
        if (raw == stable) {
            count = 0;
            return;
        }
        if (raw == pending) {
            count++;
            if (count >= TWO_KEY_TOUCH_DEBOUNCE_FRAMES) {
                stable = pending;
                count = 0;
            }
        } else {
            pending = raw;
            count = 1;
        }
    };

    updateDebounceBool(leftRaw,  leftStablePressed,  leftPendingPressed,  leftDebounceCount);
    updateDebounceBool(rightRaw, rightStablePressed, rightPendingPressed, rightDebounceCount);

    bool leftPressed  = leftStablePressed;
    bool rightPressed = rightStablePressed;

    if (leftPressed || rightPressed) {
        // 触摸键模式：抑制本帧 gamepad->read() 对 GPIO12 的映射，再输出左/右触摸映射
        gamepad->state.buttons &= ~touchpadMapping.buttonMask;
        gamepad->state.dpad    &= ~touchpadMapping.dpadMask;
        gamepad->state.aux     &= ~touchpadMapping.auxMask;
        if (touchpadMapping.isComplex)
            clearComplexMapping2Key(gamepad, *touchpadMapping.originalMapping);

        if (leftPressed)  applyMappingAndRecord(leftMapping);
        if (rightPressed) applyMappingAndRecord(rightMapping);
    } else {
        // 无触摸：输出 GPIO12 在背键映射中配置的键（与 read() 一致，可再 OR 保证一致）
        applyMappingAndRecord(touchpadMapping);
    }
}

void TwoKeyTouchpadAddon::process() {
    // 所有逻辑在 preprocess() 完成
}
