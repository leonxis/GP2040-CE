#include "addons/back_button_divider.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "gamepad/GamepadState.h"
#include "config.pb.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include "eventmanager.h"

// RP2040: ADC0 = GPIO26, ADC1 = GPIO27
static constexpr uint16_t ADC_MAX_12BIT = (1u << 12) - 1u; // 4095

// 电压阈值（以 3.3V 参考电压近似换算 ADC 计数）
// >2.9V -> 无输出
// 2.9~2.1V -> 背键2
// 2.1~1.52V -> 背键1
// <1.52V -> 1+2 同时按下
static constexpr float VREF = 3.3f;
static constexpr float V_HIGH = 2.9f;
static constexpr float V_MID = 2.1f;
static constexpr float V_LOW = 1.52f;

static constexpr uint16_t THRESH_HIGH = static_cast<uint16_t>((V_HIGH / VREF) * ADC_MAX_12BIT + 0.5f);
static constexpr uint16_t THRESH_MID  = static_cast<uint16_t>((V_MID  / VREF) * ADC_MAX_12BIT + 0.5f);
static constexpr uint16_t THRESH_LOW  = static_cast<uint16_t>((V_LOW  / VREF) * ADC_MAX_12BIT + 0.5f);

// 防抖帧数：候选档位连续 N 帧一致才更新稳定档位
static constexpr uint8_t BACK_DIVIDER_DEBOUNCE_FRAMES = 3;

// 复用 four_key_touchpad 中的思想：预解析映射，运行时只 OR mask；复杂映射再进 switch
static constexpr uint32_t KEYBOARD_KEY_ACTION_BASE_BACK = 131;
static constexpr uint8_t ADDON_MOUSE_LEFT_BIT = (1u << 0);
static constexpr uint8_t ADDON_MOUSE_RIGHT_BIT = (1u << 1);
static constexpr uint8_t ADDON_MOUSE_MIDDLE_BIT = (1u << 2);

static void parseBackMapping(const GpioMappingInfo& src, BackFastMapping& dst) {
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

static void applyBackComplex(Gamepad* gamepad, const GpioMappingInfo& m) {
    switch (m.action) {
        case GpioAction::ANALOG_DIRECTION_LS_X_NEG: gamepad->state.lx = GAMEPAD_JOYSTICK_MIN; break;
        case GpioAction::ANALOG_DIRECTION_LS_X_POS: gamepad->state.lx = GAMEPAD_JOYSTICK_MAX; break;
        case GpioAction::ANALOG_DIRECTION_LS_Y_NEG: gamepad->state.ly = GAMEPAD_JOYSTICK_MIN; break;
        case GpioAction::ANALOG_DIRECTION_LS_Y_POS: gamepad->state.ly = GAMEPAD_JOYSTICK_MAX; break;
        case GpioAction::ANALOG_DIRECTION_RS_X_NEG: gamepad->state.rx = GAMEPAD_JOYSTICK_MIN; break;
        case GpioAction::ANALOG_DIRECTION_RS_X_POS: gamepad->state.rx = GAMEPAD_JOYSTICK_MAX; break;
        case GpioAction::ANALOG_DIRECTION_RS_Y_NEG: gamepad->state.ry = GAMEPAD_JOYSTICK_MIN; break;
        case GpioAction::ANALOG_DIRECTION_RS_Y_POS: gamepad->state.ry = GAMEPAD_JOYSTICK_MAX; break;
        case GpioAction::MENU_NAVIGATION_UP:    EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_UP)); break;
        case GpioAction::MENU_NAVIGATION_DOWN:  EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_DOWN)); break;
        case GpioAction::MENU_NAVIGATION_LEFT:  EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_LEFT)); break;
        case GpioAction::MENU_NAVIGATION_RIGHT: EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_RIGHT)); break;
        case GpioAction::MENU_NAVIGATION_SELECT: EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_SELECT)); break;
        case GpioAction::MENU_NAVIGATION_BACK:   EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_BACK)); break;
        case GpioAction::MENU_NAVIGATION_TOGGLE: EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(GpioAction::MENU_NAVIGATION_TOGGLE)); break;
        default:
            if (m.action >= GpioAction::KEYBOARD_KEY_A && m.action <= GpioAction::KEYBOARD_KEY_9) {
                gamepad->addonKeyboardKeyMask |= (1ULL << (static_cast<uint32_t>(m.action) - KEYBOARD_KEY_ACTION_BASE_BACK));
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

static void clearBackComplex(Gamepad* gamepad, const GpioMappingInfo& m) {
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
                gamepad->addonKeyboardKeyMask &= ~(1ULL << (static_cast<uint32_t>(m.action) - KEYBOARD_KEY_ACTION_BASE_BACK));
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

bool BackButtonDividerAddon::available() {
    // 始终可用：由是否设置了背键映射决定是否实际输出
    return true;
}

void BackButtonDividerAddon::buildMappings() {
    const BackButtonAddonOptions& opts = Storage::getInstance().getAddonOptions().backButtonAddonOptions;
    parseBackMapping(opts.leftBack1Mapping,  leftBack1);
    parseBackMapping(opts.leftBack2Mapping,  leftBack2);
    parseBackMapping(opts.rightBack1Mapping, rightBack1);
    parseBackMapping(opts.rightBack2Mapping, rightBack2);
}

void BackButtonDividerAddon::setup() {
    // 初始化 ADC 硬件（只做一次）
    if (!adcInitialized) {
        adc_init();
        adc_gpio_init(26);
        adc_gpio_init(27);
        adcInitialized = true;
    }
    buildMappings();
}

void BackButtonDividerAddon::reinit() {
    last_out_buttons_ = last_out_dpad_ = last_out_aux_ = 0;
    last_applied_count_ = 0;
    buildMappings();
}

void BackButtonDividerAddon::applyMapping(Gamepad* gamepad, const BackFastMapping& m) {
    if (m.originalMapping == nullptr || m.originalMapping->action == GpioAction::NONE)
        return;
    gamepad->state.buttons |= m.buttonMask;
    gamepad->state.dpad    |= m.dpadMask;
    gamepad->state.aux     |= m.auxMask;
    if (m.isComplex)
        applyBackComplex(gamepad, *m.originalMapping);
}

void BackButtonDividerAddon::preprocess() {
    if (!adcInitialized)
        return;

    Gamepad* gamepad = Storage::getInstance().GetGamepad();

    // 只清除上一帧本插件实际写入的输出（与 MCP3208 CH2/CH5 思路一致），避免与 GPIO 等同键位冲突时误清
    gamepad->state.buttons &= ~last_out_buttons_;
    gamepad->state.dpad    &= ~last_out_dpad_;
    gamepad->state.aux     &= ~last_out_aux_;
    for (uint8_t i = 0; i < last_applied_count_; i++) {
        const BackFastMapping* p = last_applied_[i];
        if (p && p->isComplex && p->originalMapping)
            clearBackComplex(gamepad, *p->originalMapping);
    }
    last_out_buttons_ = last_out_dpad_ = last_out_aux_ = 0;
    last_applied_count_ = 0;

    auto recordApplied = [&](const BackFastMapping& m) {
        last_out_buttons_ |= m.buttonMask;
        last_out_dpad_    |= m.dpadMask;
        last_out_aux_     |= m.auxMask;
        if (m.isComplex && last_applied_count_ < 4)
            last_applied_[last_applied_count_++] = &m;
    };

    auto applyAndRecord = [&](const BackFastMapping& m) {
        applyMapping(gamepad, m);
        recordApplied(m);
    };

    // 读取左侧（ADC0, GPIO26）
    adc_select_input(0);
    uint16_t rawLeft = adc_read();
    // 读取右侧（ADC1, GPIO27）
    adc_select_input(1);
    uint16_t rawRight = adc_read();

    auto classifyLevel = [](uint16_t raw) -> int8_t {
        if (raw > THRESH_HIGH) return -1; // 无输出
        if (raw > THRESH_MID)  return 0;  // 背键2
        if (raw > THRESH_LOW)  return 1;  // 背键1
        return 2;                          // 背键1+2
    };

    int8_t candLeft  = classifyLevel(rawLeft);
    int8_t candRight = classifyLevel(rawRight);

    auto updateDebounce = [](int8_t candidate, int8_t& stable, int8_t& pending, uint8_t& count) {
        if (candidate == stable) {
            count = 0;
            return;
        }
        if (candidate == pending) {
            if (candidate == -1) {
                // 无输出档位：一帧即可生效，避免长时间残留
                stable = -1;
                count = 0;
            } else {
                count++;
                if (count >= BACK_DIVIDER_DEBOUNCE_FRAMES) {
                    stable = pending;
                    count = 0;
                }
            }
        } else {
            pending = candidate;
            count = 1;
        }
    };

    updateDebounce(candLeft,  leftStableLevel,  leftPendingLevel,  leftDebounceCount);
    updateDebounce(candRight, rightStableLevel, rightPendingLevel, rightDebounceCount);

    auto applyByStableLevel = [&](int8_t level, BackFastMapping& m1, BackFastMapping& m2) {
        if (level < 0) return;       // 无输出
        if (level == 0) {
            applyAndRecord(m2);
        } else if (level == 1) {
            applyAndRecord(m1);
        } else { // 2 以及其他值都视作 1+2
            applyAndRecord(m1);
            applyAndRecord(m2);
        }
    };

    applyByStableLevel(leftStableLevel,  leftBack1,  leftBack2);
    applyByStableLevel(rightStableLevel, rightBack1, rightBack2);
}

void BackButtonDividerAddon::process() {
    // 所有逻辑在 preprocess 中完成
}

