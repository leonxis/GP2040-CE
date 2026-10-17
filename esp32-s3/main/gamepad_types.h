#pragma once
#include <stdint.h>

// ===========================================================================
// 按键掩码：与 GP2040-CE 项目 headers/gamepad/GamepadState.h 完全一致
// nRF24 接收端按此解析 buttons / dpad 字段
// ===========================================================================

// DPAD 位掩码（state.dpad 低 4 位）
#define GAMEPAD_MASK_UP     (1U << 0)
#define GAMEPAD_MASK_DOWN   (1U << 1)
#define GAMEPAD_MASK_LEFT   (1U << 2)
#define GAMEPAD_MASK_RIGHT  (1U << 3)

// Buttons 位掩码（state.buttons，uint16 低 16 位）
#define GAMEPAD_MASK_B1     (1U << 0)
#define GAMEPAD_MASK_B2     (1U << 1)
#define GAMEPAD_MASK_B3     (1U << 2)
#define GAMEPAD_MASK_B4     (1U << 3)
#define GAMEPAD_MASK_L1     (1U << 4)
#define GAMEPAD_MASK_R1     (1U << 5)
#define GAMEPAD_MASK_L2     (1U << 6)
#define GAMEPAD_MASK_R2     (1U << 7)
#define GAMEPAD_MASK_S1     (1U << 8)
#define GAMEPAD_MASK_S2     (1U << 9)
#define GAMEPAD_MASK_L3     (1U << 10)
#define GAMEPAD_MASK_R3     (1U << 11)
#define GAMEPAD_MASK_A1     (1U << 12)
#define GAMEPAD_MASK_A2     (1U << 13)
#define GAMEPAD_MASK_A3     (1U << 14)
#define GAMEPAD_MASK_A4     (1U << 15)

// 摇杆 / 扳机范围
#define GAMEPAD_JOYSTICK_MIN    0
#define GAMEPAD_JOYSTICK_MID    0x7FFF
#define GAMEPAD_JOYSTICK_MAX    0xFFFF
#define GAMEPAD_TRIGGER_MIN     0
#define GAMEPAD_TRIGGER_MAX     0xFF

// InputMode 枚举值（与 proto/enums.proto 一致）
#define INPUT_MODE_XINPUT       0
#define INPUT_MODE_SWITCH       1
#define INPUT_MODE_PS4          4
