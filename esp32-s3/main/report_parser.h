#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hid_report_parser.h"

// ===========================================================================
// 手柄输入状态（统一格式，对应 GP2040 state.buttons/dpad/lx/ly/rx/ry/lt/rt）
// ===========================================================================
typedef struct {
    uint16_t buttons;   // GAMEPAD_MASK_* 位掩码
    uint8_t  dpad;      // GAMEPAD_MASK_UP/DOWN/LEFT/RIGHT 位掩码
    uint16_t lx, ly, rx, ry;  // 0~65535，中点 32768
    uint8_t  lt, rt;    // 0~255
} GamepadState;

// 模式类型
typedef enum {
    GP_MODE_UNKNOWN = 0,
    GP_MODE_SWITCH,       // NS（HORI POKKEN 或通用 HID）
    GP_MODE_PS4,          // DS（DualShock 4）
    GP_MODE_XINPUT,       // Xbox 360
} GamepadMode;

// 三种模式硬编码解析
void parse_switch_report(const uint8_t *buf, uint16_t len, GamepadState *out);
void parse_ps4_report(const uint8_t *buf, uint16_t len, GamepadState *out);
void parse_xinput_report(const uint8_t *buf, uint16_t len, GamepadState *out);

// 通用 HID 解析（基于描述符解析结果）
void parse_generic_hid(const uint8_t *buf, uint16_t len, const HidLayout *layout,
                       GamepadState *out);

// hat 值（0-7 方向，8=居中）转 dpad 位掩码
uint8_t hat_to_dpad(uint8_t hat);
