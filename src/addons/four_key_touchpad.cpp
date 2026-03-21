#include "addons/four_key_touchpad.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "gamepad/GamepadState.h"
#include "config.pb.h"
#include "hardware/gpio.h"
#include "helper.h"
#include "pico/time.h"
#include "eventmanager.h"

// BS814A-2 串行协议：8 个时钟一组。Clock 低电平时芯片准备数据，变高后主机从 Data 读一位。
// 8 位数据：Bit0=Key1, Bit1=Key2, Bit2=Key3, Bit3=Key4；0=按下, 1=松键。Bit7=停止位恒为 1。
#define TOUCHPAD_ENABLE_PIN 12
#define BS814A_POLL_INTERVAL_US  1000
#define BS814A_CLOCK_HALF_CYCLE_US 20
#define BS814A_BITS_PER_FRAME 2
#define ENABLE_DEBOUNCE_MS 3
static constexpr uint32_t KEYBOARD_KEY_ACTION_BASE = 131;

// 映射在 setup/reinit 时解析一次，运行时不做 switch：普通按键/方向/FN/组合键 → buttonMask/dpadMask/auxMask，仅 OR。
// 仅 ANALOG/MENU/KEYBOARD 标为 isComplex，运行时在 applyComplexMapping 中按需进入。
static void parseMapping(const GpioMappingInfo& src, FastMapping& dst) {
    dst.buttonMask = 0;
    dst.dpadMask = 0;
    dst.auxMask = 0;
    dst.isComplex = false;
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
        case GpioAction::BUTTON_PRESS_UP:    dst.dpadMask   |= GAMEPAD_MASK_UP; break;
        case GpioAction::BUTTON_PRESS_DOWN:  dst.dpadMask   |= GAMEPAD_MASK_DOWN; break;
        case GpioAction::BUTTON_PRESS_LEFT:  dst.dpadMask   |= GAMEPAD_MASK_LEFT; break;
        case GpioAction::BUTTON_PRESS_RIGHT: dst.dpadMask   |= GAMEPAD_MASK_RIGHT; break;
        case GpioAction::BUTTON_PRESS_B1:   dst.buttonMask |= GAMEPAD_MASK_B1; break;
        case GpioAction::BUTTON_PRESS_B2:   dst.buttonMask |= GAMEPAD_MASK_B2; break;
        case GpioAction::BUTTON_PRESS_B3:   dst.buttonMask |= GAMEPAD_MASK_B3; break;
        case GpioAction::BUTTON_PRESS_B4:   dst.buttonMask |= GAMEPAD_MASK_B4; break;
        case GpioAction::BUTTON_PRESS_L1:   dst.buttonMask |= GAMEPAD_MASK_L1; break;
        case GpioAction::BUTTON_PRESS_R1:   dst.buttonMask |= GAMEPAD_MASK_R1; break;
        case GpioAction::BUTTON_PRESS_L2:   dst.buttonMask |= GAMEPAD_MASK_L2; break;
        case GpioAction::BUTTON_PRESS_R2:   dst.buttonMask |= GAMEPAD_MASK_R2; break;
        case GpioAction::BUTTON_PRESS_S1:   dst.buttonMask |= GAMEPAD_MASK_S1; break;
        case GpioAction::BUTTON_PRESS_S2:   dst.buttonMask |= GAMEPAD_MASK_S2; break;
        case GpioAction::BUTTON_PRESS_L3:   dst.buttonMask |= GAMEPAD_MASK_L3; break;
        case GpioAction::BUTTON_PRESS_R3:   dst.buttonMask |= GAMEPAD_MASK_R3; break;
        case GpioAction::BUTTON_PRESS_A1:   dst.buttonMask |= GAMEPAD_MASK_A1; break;
        case GpioAction::BUTTON_PRESS_A2:   dst.buttonMask |= GAMEPAD_MASK_A2; break;
        case GpioAction::BUTTON_PRESS_A3:   dst.buttonMask |= GAMEPAD_MASK_A3; break;
        case GpioAction::BUTTON_PRESS_A4:   dst.buttonMask |= GAMEPAD_MASK_A4; break;
        case GpioAction::BUTTON_PRESS_E1:   dst.buttonMask |= GAMEPAD_MASK_E1; break;
        case GpioAction::BUTTON_PRESS_E2:   dst.buttonMask |= GAMEPAD_MASK_E2; break;
        case GpioAction::BUTTON_PRESS_E3:   dst.buttonMask |= GAMEPAD_MASK_E3; break;
        case GpioAction::BUTTON_PRESS_E4:   dst.buttonMask |= GAMEPAD_MASK_E4; break;
        case GpioAction::BUTTON_PRESS_E5:   dst.buttonMask |= GAMEPAD_MASK_E5; break;
        case GpioAction::BUTTON_PRESS_E6:   dst.buttonMask |= GAMEPAD_MASK_E6; break;
        case GpioAction::BUTTON_PRESS_E7:   dst.buttonMask |= GAMEPAD_MASK_E7; break;
        case GpioAction::BUTTON_PRESS_E8:   dst.buttonMask |= GAMEPAD_MASK_E8; break;
        case GpioAction::BUTTON_PRESS_E9:   dst.buttonMask |= GAMEPAD_MASK_E9; break;
        case GpioAction::BUTTON_PRESS_E10:  dst.buttonMask |= GAMEPAD_MASK_E10; break;
        case GpioAction::BUTTON_PRESS_E11:  dst.buttonMask |= GAMEPAD_MASK_E11; break;
        case GpioAction::BUTTON_PRESS_E12:  dst.buttonMask |= GAMEPAD_MASK_E12; break;
        case GpioAction::BUTTON_PRESS_FN:   dst.auxMask    |= AUX_MASK_FUNCTION; break;
        default:
            dst.isComplex = true;
            break;
    }
}

// 仅处理复杂映射：ANALOG / MENU / KEYBOARD
static void applyComplexMapping(Gamepad* gamepad, const GpioMappingInfo& m) {
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
            if (m.action >= GpioAction::KEYBOARD_KEY_A && m.action <= GpioAction::KEYBOARD_KEY_9)
                gamepad->addonKeyboardKeyMask |= (1ULL << (static_cast<uint32_t>(m.action) - KEYBOARD_KEY_ACTION_BASE));
            break;
    }
}

static void clearComplexMapping(Gamepad* gamepad, const GpioMappingInfo& m) {
    switch (m.action) {
        case GpioAction::ANALOG_DIRECTION_LS_X_NEG:
        case GpioAction::ANALOG_DIRECTION_LS_X_POS:
            gamepad->state.lx = GAMEPAD_JOYSTICK_MID;
            break;
        case GpioAction::ANALOG_DIRECTION_LS_Y_NEG:
        case GpioAction::ANALOG_DIRECTION_LS_Y_POS:
            gamepad->state.ly = GAMEPAD_JOYSTICK_MID;
            break;
        case GpioAction::ANALOG_DIRECTION_RS_X_NEG:
        case GpioAction::ANALOG_DIRECTION_RS_X_POS:
            gamepad->state.rx = GAMEPAD_JOYSTICK_MID;
            break;
        case GpioAction::ANALOG_DIRECTION_RS_Y_NEG:
        case GpioAction::ANALOG_DIRECTION_RS_Y_POS:
            gamepad->state.ry = GAMEPAD_JOYSTICK_MID;
            break;
        case GpioAction::MENU_NAVIGATION_UP:
        case GpioAction::MENU_NAVIGATION_DOWN:
        case GpioAction::MENU_NAVIGATION_LEFT:
        case GpioAction::MENU_NAVIGATION_RIGHT:
        case GpioAction::MENU_NAVIGATION_SELECT:
        case GpioAction::MENU_NAVIGATION_BACK:
        case GpioAction::MENU_NAVIGATION_TOGGLE:
            break;
        default:
            if (m.action >= GpioAction::KEYBOARD_KEY_A && m.action <= GpioAction::KEYBOARD_KEY_9)
                gamepad->addonKeyboardKeyMask &= ~(1ULL << (static_cast<uint32_t>(m.action) - KEYBOARD_KEY_ACTION_BASE));
            break;
    }
}

bool FourKeyTouchpadAddon::available() {
    const FourKeyTouchpadOptions& opts = Storage::getInstance().getAddonOptions().fourKeyTouchpadOptions;
    if (!opts.enabled) return false;
    const PeripheralOptions& peri = Storage::getInstance().getPeripheralOptions();
    return isValidPin(peri.blockI2C1.sda) && isValidPin(peri.blockI2C1.scl);
}

void FourKeyTouchpadAddon::buildMappings() {
    const FourKeyTouchpadOptions& opts = Storage::getInstance().getAddonOptions().fourKeyTouchpadOptions;
    parseMapping(opts.key1Mapping, fastMappings[0]);
    parseMapping(opts.key2Mapping, fastMappings[1]);
    parseMapping(opts.key3Mapping, fastMappings[2]);
    parseMapping(opts.key4Mapping, fastMappings[3]);
}

void FourKeyTouchpadAddon::setup() {
    const PeripheralOptions& peri = Storage::getInstance().getPeripheralOptions();
    pin_sck = peri.blockI2C1.sda;
    pin_data = peri.blockI2C1.scl;
    if (!isValidPin(pin_sck) || !isValidPin(pin_data))
        return;
    gpio_init((uint)pin_sck);
    gpio_set_dir((uint)pin_sck, GPIO_OUT);
    gpio_put((uint)pin_sck, 0);
    gpio_init((uint)pin_data);
    gpio_set_dir((uint)pin_data, GPIO_IN);
    gpio_pull_up((uint)pin_data);
    gpio_init(TOUCHPAD_ENABLE_PIN);
    gpio_set_dir(TOUCHPAD_ENABLE_PIN, GPIO_IN);
    gpio_pull_up(TOUCHPAD_ENABLE_PIN);
    buildMappings();
    enableRawLast = !gpio_get(TOUCHPAD_ENABLE_PIN);
    enableChangeTime = 0;
    enableStable = enableRawLast;
}

void FourKeyTouchpadAddon::reinit() {
    buildMappings();
    last_out_buttons_ = last_out_dpad_ = last_out_aux_ = 0;
    last_applied_complex_count_ = 0;
}

void FourKeyTouchpadAddon::preprocess() {
    if (!isValidPin(pin_sck) || !isValidPin(pin_data))
        return;

    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    uint32_t now = time_us_32();

    // ① BS814A 分帧读取（始终运行）
    if ((uint32_t)(now - lastPollTime) >= BS814A_POLL_INTERVAL_US) {
        lastPollTime = now;
        uint sck = (uint)pin_sck;
        uint sda = (uint)pin_data;
        const int startBit = readPhase * BS814A_BITS_PER_FRAME;

        gpio_put(sck, 0);
        busy_wait_us(BS814A_CLOCK_HALF_CYCLE_US);
        gpio_put(sck, 1);
        if (gpio_get(sda)) partialByte |= (1u << startBit);
        busy_wait_us(BS814A_CLOCK_HALF_CYCLE_US);

        gpio_put(sck, 0);
        busy_wait_us(BS814A_CLOCK_HALF_CYCLE_US);
        gpio_put(sck, 1);
        if (gpio_get(sda)) partialByte |= (1u << (startBit + 1));
        busy_wait_us(BS814A_CLOCK_HALF_CYCLE_US);

        readPhase++;
        if (readPhase >= 4) {
            readPhase = 0;
            uint8_t byte = partialByte;
            partialByte = 0;
            if (byte & 0x80)
                lastKeyNibble = byte & 0x0F;
        }
    }

    // ② 使能键 3ms 防抖（低有效，仅作触摸使能，不输出按键）
    bool enableRaw = !gpio_get(TOUCHPAD_ENABLE_PIN);
    if (enableRaw != enableRawLast) {
        enableRawLast = enableRaw;
        enableChangeTime = now;
    }
    if ((now - enableChangeTime) >= (ENABLE_DEBOUNCE_MS * 1000u))
        enableStable = enableRaw;

    // ③ 输出：先撤销上一帧本插件输出；使能成立时再按 lastKeyNibble 输出 4 键映射（使能键本身不映射为按键）
    uint8_t keyNibble = lastKeyNibble;

    // ① 撤销上一帧本插件实际输出过的按键位（而不是清除配置可能涉及的整段 mask）
    gamepad->state.buttons &= ~last_out_buttons_;
    gamepad->state.dpad    &= ~last_out_dpad_;
    gamepad->state.aux     &= ~last_out_aux_;
    for (uint8_t i = 0; i < last_applied_complex_count_; i++) {
        const FastMapping* p = last_applied_complex_[i];
        if (p && p->isComplex && p->originalMapping) {
            clearComplexMapping(gamepad, *p->originalMapping);
        }
    }
    last_out_buttons_ = last_out_dpad_ = last_out_aux_ = 0;
    last_applied_complex_count_ = 0;

    // ② 使能不成立时：本帧不输出任何触摸键
    if (!enableStable)
        return;

    // ③ 使能成立时：按 lastKeyNibble 输出按键映射
    for (int i = 0; i < 4; i++) {
        if (!(keyNibble & (1u << i))) { // bit=0 => pressed
            gamepad->state.buttons |= fastMappings[i].buttonMask;
            gamepad->state.dpad    |= fastMappings[i].dpadMask;
            gamepad->state.aux     |= fastMappings[i].auxMask;

            last_out_buttons_ |= fastMappings[i].buttonMask;
            last_out_dpad_    |= fastMappings[i].dpadMask;
            last_out_aux_     |= fastMappings[i].auxMask;

            if (fastMappings[i].isComplex) {
                applyComplexMapping(gamepad, *fastMappings[i].originalMapping);
                // 仅记录本帧实际输出过的复杂映射用于下一帧撤销
                if (last_applied_complex_count_ < 4)
                    last_applied_complex_[last_applied_complex_count_++] = &fastMappings[i];
            }
        }
    }
}

void FourKeyTouchpadAddon::process() {
    // 使能键与触摸键均在 preprocess() 中输出，此处无需处理
}
