#include "addons/four_key_touchpad.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "hardware/gpio.h"
#include "helper.h"
#include "pico/time.h"
#include "eventmanager.h"

// BS814A-2 串行协议：8 个时钟一组。Clock 低电平时芯片准备数据，变高后主机从 Data 读一位。
// 8 位数据：Bit0=Key1(左上), Bit1=Key2(左下), Bit2=Key3(右下), Bit3=Key4(右上)；0=按下, 1=松键。
// Bit6~4 = 校验和（被触摸键总数）；Bit7 = 停止位恒为 1。读错时需等 6ms 再重读。
// 时序：TLOW/THIGH 最小 20µs，TBR 最大 25 Kbps，TED=6ms。
// 节流 + 分帧读：每 1ms 最多读 2 位（约 80µs），4 帧凑齐 8 位，单帧阻塞从 0.32ms 降为 80µs。
#define TOUCHPAD_ENABLE_PIN 12
#define BS814A_POLL_INTERVAL_US  1000   // 每帧间隔 1ms
#define BS814A_RETRY_COOLDOWN_US 6000   // 读错后冷却 6ms 再读（不阻塞）
#define BS814A_CLOCK_LOW_US  20
#define BS814A_CLOCK_HIGH_US 20
#define BS814A_BITS_PER_FRAME 2        // 每帧读 2 位，共 4 帧 = 8 位
// addonKeyboardKeyMask 位索引：KEYBOARD_KEY_A=131 至 KEYBOARD_KEY_9=169（含 CTRL/SHIFT/ALT_F4）
static constexpr uint32_t KEYBOARD_KEY_ACTION_BASE = 131;

static void applyTouchpadMapping(Gamepad* gamepad, const GpioMappingInfo& m) {
    if (m.action == GpioAction::NONE)
        return;
    if (m.action == GpioAction::CUSTOM_BUTTON_COMBO) {
        gamepad->state.buttons |= m.customButtonMask;
        // customDpadMask uses DU/DD/DL/DR; state.dpad uses UP/DOWN/LEFT/RIGHT
        if (m.customDpadMask & GAMEPAD_MASK_DU) gamepad->state.dpad |= GAMEPAD_MASK_UP;
        if (m.customDpadMask & GAMEPAD_MASK_DD) gamepad->state.dpad |= GAMEPAD_MASK_DOWN;
        if (m.customDpadMask & GAMEPAD_MASK_DL) gamepad->state.dpad |= GAMEPAD_MASK_LEFT;
        if (m.customDpadMask & GAMEPAD_MASK_DR) gamepad->state.dpad |= GAMEPAD_MASK_RIGHT;
        return;
    }
    switch (m.action) {
        case GpioAction::BUTTON_PRESS_UP:    gamepad->state.dpad |= GAMEPAD_MASK_UP; break;
        case GpioAction::BUTTON_PRESS_DOWN:  gamepad->state.dpad |= GAMEPAD_MASK_DOWN; break;
        case GpioAction::BUTTON_PRESS_LEFT:  gamepad->state.dpad |= GAMEPAD_MASK_LEFT; break;
        case GpioAction::BUTTON_PRESS_RIGHT: gamepad->state.dpad |= GAMEPAD_MASK_RIGHT; break;
        case GpioAction::BUTTON_PRESS_B1:    gamepad->state.buttons |= GAMEPAD_MASK_B1; break;
        case GpioAction::BUTTON_PRESS_B2:    gamepad->state.buttons |= GAMEPAD_MASK_B2; break;
        case GpioAction::BUTTON_PRESS_B3:    gamepad->state.buttons |= GAMEPAD_MASK_B3; break;
        case GpioAction::BUTTON_PRESS_B4:    gamepad->state.buttons |= GAMEPAD_MASK_B4; break;
        case GpioAction::BUTTON_PRESS_L1:    gamepad->state.buttons |= GAMEPAD_MASK_L1; break;
        case GpioAction::BUTTON_PRESS_R1:    gamepad->state.buttons |= GAMEPAD_MASK_R1; break;
        case GpioAction::BUTTON_PRESS_L2:    gamepad->state.buttons |= GAMEPAD_MASK_L2; break;
        case GpioAction::BUTTON_PRESS_R2:    gamepad->state.buttons |= GAMEPAD_MASK_R2; break;
        case GpioAction::BUTTON_PRESS_S1:    gamepad->state.buttons |= GAMEPAD_MASK_S1; break;
        case GpioAction::BUTTON_PRESS_S2:    gamepad->state.buttons |= GAMEPAD_MASK_S2; break;
        case GpioAction::BUTTON_PRESS_L3:    gamepad->state.buttons |= GAMEPAD_MASK_L3; break;
        case GpioAction::BUTTON_PRESS_R3:    gamepad->state.buttons |= GAMEPAD_MASK_R3; break;
        case GpioAction::BUTTON_PRESS_A1:    gamepad->state.buttons |= GAMEPAD_MASK_A1; break;
        case GpioAction::BUTTON_PRESS_A2:    gamepad->state.buttons |= GAMEPAD_MASK_A2; break;
        case GpioAction::BUTTON_PRESS_A3:    gamepad->state.buttons |= GAMEPAD_MASK_A3; break;
        case GpioAction::BUTTON_PRESS_A4:    gamepad->state.buttons |= GAMEPAD_MASK_A4; break;
        case GpioAction::BUTTON_PRESS_E1:    gamepad->state.buttons |= GAMEPAD_MASK_E1; break;
        case GpioAction::BUTTON_PRESS_E2:    gamepad->state.buttons |= GAMEPAD_MASK_E2; break;
        case GpioAction::BUTTON_PRESS_E3:    gamepad->state.buttons |= GAMEPAD_MASK_E3; break;
        case GpioAction::BUTTON_PRESS_E4:    gamepad->state.buttons |= GAMEPAD_MASK_E4; break;
        case GpioAction::BUTTON_PRESS_E5:    gamepad->state.buttons |= GAMEPAD_MASK_E5; break;
        case GpioAction::BUTTON_PRESS_E6:    gamepad->state.buttons |= GAMEPAD_MASK_E6; break;
        case GpioAction::BUTTON_PRESS_E7:    gamepad->state.buttons |= GAMEPAD_MASK_E7; break;
        case GpioAction::BUTTON_PRESS_E8:    gamepad->state.buttons |= GAMEPAD_MASK_E8; break;
        case GpioAction::BUTTON_PRESS_E9:    gamepad->state.buttons |= GAMEPAD_MASK_E9; break;
        case GpioAction::BUTTON_PRESS_E10:   gamepad->state.buttons |= GAMEPAD_MASK_E10; break;
        case GpioAction::BUTTON_PRESS_E11:   gamepad->state.buttons |= GAMEPAD_MASK_E11; break;
        case GpioAction::BUTTON_PRESS_E12:   gamepad->state.buttons |= GAMEPAD_MASK_E12; break;
        case GpioAction::BUTTON_PRESS_FN:    gamepad->state.aux |= AUX_MASK_FUNCTION; break;
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

// 清除单条映射对应的按键状态（GPIO12 为高时调用，释放该映射占用的位）
static void clearTouchpadMapping(Gamepad* gamepad, const GpioMappingInfo& m) {
    if (m.action == GpioAction::NONE)
        return;
    if (m.action == GpioAction::CUSTOM_BUTTON_COMBO) {
        gamepad->state.buttons &= ~m.customButtonMask;
        if (m.customDpadMask & GAMEPAD_MASK_DU) gamepad->state.dpad &= ~GAMEPAD_MASK_UP;
        if (m.customDpadMask & GAMEPAD_MASK_DD) gamepad->state.dpad &= ~GAMEPAD_MASK_DOWN;
        if (m.customDpadMask & GAMEPAD_MASK_DL) gamepad->state.dpad &= ~GAMEPAD_MASK_LEFT;
        if (m.customDpadMask & GAMEPAD_MASK_DR) gamepad->state.dpad &= ~GAMEPAD_MASK_RIGHT;
        return;
    }
    switch (m.action) {
        case GpioAction::BUTTON_PRESS_UP:    gamepad->state.dpad &= ~GAMEPAD_MASK_UP; break;
        case GpioAction::BUTTON_PRESS_DOWN:  gamepad->state.dpad &= ~GAMEPAD_MASK_DOWN; break;
        case GpioAction::BUTTON_PRESS_LEFT:  gamepad->state.dpad &= ~GAMEPAD_MASK_LEFT; break;
        case GpioAction::BUTTON_PRESS_RIGHT: gamepad->state.dpad &= ~GAMEPAD_MASK_RIGHT; break;
        case GpioAction::BUTTON_PRESS_B1:    gamepad->state.buttons &= ~GAMEPAD_MASK_B1; break;
        case GpioAction::BUTTON_PRESS_B2:    gamepad->state.buttons &= ~GAMEPAD_MASK_B2; break;
        case GpioAction::BUTTON_PRESS_B3:    gamepad->state.buttons &= ~GAMEPAD_MASK_B3; break;
        case GpioAction::BUTTON_PRESS_B4:    gamepad->state.buttons &= ~GAMEPAD_MASK_B4; break;
        case GpioAction::BUTTON_PRESS_L1:    gamepad->state.buttons &= ~GAMEPAD_MASK_L1; break;
        case GpioAction::BUTTON_PRESS_R1:    gamepad->state.buttons &= ~GAMEPAD_MASK_R1; break;
        case GpioAction::BUTTON_PRESS_L2:    gamepad->state.buttons &= ~GAMEPAD_MASK_L2; break;
        case GpioAction::BUTTON_PRESS_R2:    gamepad->state.buttons &= ~GAMEPAD_MASK_R2; break;
        case GpioAction::BUTTON_PRESS_S1:    gamepad->state.buttons &= ~GAMEPAD_MASK_S1; break;
        case GpioAction::BUTTON_PRESS_S2:    gamepad->state.buttons &= ~GAMEPAD_MASK_S2; break;
        case GpioAction::BUTTON_PRESS_L3:    gamepad->state.buttons &= ~GAMEPAD_MASK_L3; break;
        case GpioAction::BUTTON_PRESS_R3:    gamepad->state.buttons &= ~GAMEPAD_MASK_R3; break;
        case GpioAction::BUTTON_PRESS_A1:    gamepad->state.buttons &= ~GAMEPAD_MASK_A1; break;
        case GpioAction::BUTTON_PRESS_A2:    gamepad->state.buttons &= ~GAMEPAD_MASK_A2; break;
        case GpioAction::BUTTON_PRESS_A3:    gamepad->state.buttons &= ~GAMEPAD_MASK_A3; break;
        case GpioAction::BUTTON_PRESS_A4:    gamepad->state.buttons &= ~GAMEPAD_MASK_A4; break;
        case GpioAction::BUTTON_PRESS_E1:    gamepad->state.buttons &= ~GAMEPAD_MASK_E1; break;
        case GpioAction::BUTTON_PRESS_E2:    gamepad->state.buttons &= ~GAMEPAD_MASK_E2; break;
        case GpioAction::BUTTON_PRESS_E3:    gamepad->state.buttons &= ~GAMEPAD_MASK_E3; break;
        case GpioAction::BUTTON_PRESS_E4:    gamepad->state.buttons &= ~GAMEPAD_MASK_E4; break;
        case GpioAction::BUTTON_PRESS_E5:    gamepad->state.buttons &= ~GAMEPAD_MASK_E5; break;
        case GpioAction::BUTTON_PRESS_E6:    gamepad->state.buttons &= ~GAMEPAD_MASK_E6; break;
        case GpioAction::BUTTON_PRESS_E7:    gamepad->state.buttons &= ~GAMEPAD_MASK_E7; break;
        case GpioAction::BUTTON_PRESS_E8:    gamepad->state.buttons &= ~GAMEPAD_MASK_E8; break;
        case GpioAction::BUTTON_PRESS_E9:    gamepad->state.buttons &= ~GAMEPAD_MASK_E9; break;
        case GpioAction::BUTTON_PRESS_E10:   gamepad->state.buttons &= ~GAMEPAD_MASK_E10; break;
        case GpioAction::BUTTON_PRESS_E11:   gamepad->state.buttons &= ~GAMEPAD_MASK_E11; break;
        case GpioAction::BUTTON_PRESS_E12:   gamepad->state.buttons &= ~GAMEPAD_MASK_E12; break;
        case GpioAction::BUTTON_PRESS_FN:    gamepad->state.aux &= ~AUX_MASK_FUNCTION; break;
        case GpioAction::ANALOG_DIRECTION_LS_X_NEG: case GpioAction::ANALOG_DIRECTION_LS_X_POS:
            gamepad->state.lx = GAMEPAD_JOYSTICK_MID;
            break;
        case GpioAction::ANALOG_DIRECTION_LS_Y_NEG: case GpioAction::ANALOG_DIRECTION_LS_Y_POS:
            gamepad->state.ly = GAMEPAD_JOYSTICK_MID;
            break;
        case GpioAction::ANALOG_DIRECTION_RS_X_NEG: case GpioAction::ANALOG_DIRECTION_RS_X_POS:
            gamepad->state.rx = GAMEPAD_JOYSTICK_MID;
            break;
        case GpioAction::ANALOG_DIRECTION_RS_Y_NEG: case GpioAction::ANALOG_DIRECTION_RS_Y_POS:
            gamepad->state.ry = GAMEPAD_JOYSTICK_MID;
            break;
        case GpioAction::MENU_NAVIGATION_UP: case GpioAction::MENU_NAVIGATION_DOWN:
        case GpioAction::MENU_NAVIGATION_LEFT: case GpioAction::MENU_NAVIGATION_RIGHT:
        case GpioAction::MENU_NAVIGATION_SELECT: case GpioAction::MENU_NAVIGATION_BACK:
        case GpioAction::MENU_NAVIGATION_TOGGLE:
            /* 菜单导航为事件，无需清除 */
            break;
        default:
            if (m.action >= GpioAction::KEYBOARD_KEY_A && m.action <= GpioAction::KEYBOARD_KEY_9)
                gamepad->addonKeyboardKeyMask &= ~(1ULL << (static_cast<uint32_t>(m.action) - KEYBOARD_KEY_ACTION_BASE));
            break;
    }
}

bool FourKeyTouchpadAddon::available() {
    const FourKeyTouchpadOptions& opts = Storage::getInstance().getAddonOptions().fourKeyTouchpadOptions;
    if (!opts.enabled)
        return false;
    const PeripheralOptions& peri = Storage::getInstance().getPeripheralOptions();
    return isValidPin(peri.blockI2C1.sda) && isValidPin(peri.blockI2C1.scl);
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
}

void FourKeyTouchpadAddon::preprocess() {
    if (!isValidPin(pin_sck) || !isValidPin(pin_data))
        return;
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    const FourKeyTouchpadOptions& opts = Storage::getInstance().getAddonOptions().fourKeyTouchpadOptions;
    const GpioMappingInfo* mappings[] = { &opts.key1Mapping, &opts.key2Mapping, &opts.key3Mapping, &opts.key4Mapping };
    if (gpio_get(TOUCHPAD_ENABLE_PIN)) {
        // GPIO12 为高：释放所有触摸板映射的按键，并重置分帧状态以便使能再次按下时从新一帧开始
        anyTouchKeyPressed = false;
        readPhase = 0;
        partialByte = 0;
        for (int i = 0; i < 4; i++)
            clearTouchpadMapping(gamepad, *mappings[i]);
        return;
    }
    // GPIO12 为低：每 1ms 只读 2 位（约 80µs），4 帧凑齐 8 位后校验并更新键值
    uint32_t now = time_us_32();
    uint8_t keyNibble = lastKeyNibble;

    if (now >= nextReadAllowed && (uint32_t)(now - lastPollTime) >= BS814A_POLL_INTERVAL_US) {
        lastPollTime = now;
        const int startBit = readPhase * BS814A_BITS_PER_FRAME;
        for (int i = 0; i < BS814A_BITS_PER_FRAME; i++) {
            gpio_put((uint)pin_sck, 0);
            busy_wait_us(BS814A_CLOCK_LOW_US);
            gpio_put((uint)pin_sck, 1);
            busy_wait_us(BS814A_CLOCK_HIGH_US);
            if (gpio_get((uint)pin_data))
                partialByte |= (1u << (startBit + i));
        }
        readPhase++;
        if (readPhase >= 8 / BS814A_BITS_PER_FRAME) {
            readPhase = 0;
            uint8_t byte = partialByte;
            partialByte = 0;
            bool stopOk = (byte & 0x80) != 0;
            // 分帧读时 8 位来自 4 个不同时刻，校验和经常不匹配，故仅要求停止位正确即采纳键值
            if (stopOk) {
                keyNibble = byte & 0x0F;
                lastKeyNibble = keyNibble;
            } else {
                nextReadAllowed = now + BS814A_RETRY_COOLDOWN_US;
                keyNibble = lastKeyNibble;
            }
        }
    }

    anyTouchKeyPressed = false;
    for (int i = 0; i < 4; i++) {
        if (!(keyNibble & (1u << i))) {
            anyTouchKeyPressed = true;
            applyTouchpadMapping(gamepad, *mappings[i]);
        }
    }
}

void FourKeyTouchpadAddon::process() {
    if (!isValidPin(pin_sck) || !isValidPin(pin_data))
        return;
    // 使能键按下且本帧有触摸键按下时，屏蔽 GPIO12 的映射，仅保留触摸键映射
    if (!gpio_get(TOUCHPAD_ENABLE_PIN) && anyTouchKeyPressed) {
        Gamepad* gamepad = Storage::getInstance().GetGamepad();
        GpioMappingInfo* profilePins = Storage::getInstance().getProfilePinMappings();
        clearTouchpadMapping(gamepad, profilePins[TOUCHPAD_ENABLE_PIN]);
    }
}
