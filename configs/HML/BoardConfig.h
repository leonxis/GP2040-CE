/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#ifndef PICO_BOARD_CONFIG_H_
#define PICO_BOARD_CONFIG_H_

#include "enums.pb.h"
#include "class/hid/hid.h"

#define BOARD_CONFIG_LABEL "HML"

// Main pin mapping Configuration
//                                                  // GP2040 | Xinput | Switch  | PS3/4/5  | Dinput | Arcade |
#define GPIO_PIN_11 GpioAction::BUTTON_PRESS_UP     // UP     | UP     | UP      | UP       | UP     | UP     |
#define GPIO_PIN_10 GpioAction::BUTTON_PRESS_LEFT   // LEFT   | LEFT   | LEFT    | LEFT     | LEFT   | LEFT   |
#define GPIO_PIN_12 GpioAction::BUTTON_PRESS_RIGHT  // RIGHT  | RIGHT  | RIGHT   | RIGHT    | RIGHT  | RIGHT  |
#define GPIO_PIN_14 GpioAction::BUTTON_PRESS_B1     // B1     | A      | B       | Cross    | 2      | K1     |
#define GPIO_PIN_00 GpioAction::BUTTON_PRESS_B2     // B2     | B      | A       | Circle   | 3      | K2     |
#define GPIO_PIN_02 GpioAction::BUTTON_PRESS_R2     // R2     | RT     | ZR      | R2       | 8      | K3     |
#define GPIO_PIN_01 GpioAction::BUTTON_PRESS_L2     // L2     | LT     | ZL      | L2       | 7      | K4     |
#define GPIO_PIN_03 GpioAction::BUTTON_PRESS_B3     // B3     | X      | Y       | Square   | 1      | P1     |
#define GPIO_PIN_04 GpioAction::BUTTON_PRESS_B4     // B4     | Y      | X       | Triangle | 4      | P2     |
#define GPIO_PIN_13 GpioAction::BUTTON_PRESS_S1     // S1     | Back   | Minus   | Select   | 9      | Coin   |
#define GPIO_PIN_15 GpioAction::BUTTON_PRESS_S2     // S2     | Start  | Plus    | Start    | 10     | Start  |
#define GPIO_PIN_05 GpioAction::MENU_NAVIGATION_TOGGLE  
// Setting GPIO pins to assigned by add-on
//
#define GPIO_PIN_06 GpioAction::ASSIGNED_TO_ADDON
#define GPIO_PIN_07 GpioAction::ASSIGNED_TO_ADDON
#define GPIO_PIN_08 GpioAction::ASSIGNED_TO_ADDON
#define GPIO_PIN_09 GpioAction::ASSIGNED_TO_ADDON
#define GPIO_PIN_16 GpioAction::ASSIGNED_TO_ADDON
#define GPIO_PIN_26 GpioAction::ASSIGNED_TO_ADDON
#define GPIO_PIN_27 GpioAction::ASSIGNED_TO_ADDON
#define GPIO_PIN_28 GpioAction::ASSIGNED_TO_ADDON
#define GPIO_PIN_29 GpioAction::ASSIGNED_TO_ADDON

// Keyboard Mapping Configuration
//                                            // GP2040 | Xinput | Switch  | PS3/4/5  | Dinput | Arcade |
#define KEY_DPAD_UP     HID_KEY_ARROW_UP      // UP     | UP     | UP      | UP       | UP     | UP     |
#define KEY_DPAD_DOWN   HID_KEY_ARROW_DOWN    // DOWN   | DOWN   | DOWN    | DOWN     | DOWN   | DOWN   |
#define KEY_DPAD_RIGHT  HID_KEY_ARROW_RIGHT   // RIGHT  | RIGHT  | RIGHT   | RIGHT    | RIGHT  | RIGHT  |
#define KEY_DPAD_LEFT   HID_KEY_ARROW_LEFT    // LEFT   | LEFT   | LEFT    | LEFT     | LEFT   | LEFT   |
#define KEY_BUTTON_B1   HID_KEY_SHIFT_LEFT    // B1     | A      | B       | Cross    | 2      | K1     |
#define KEY_BUTTON_B2   HID_KEY_Z             // B2     | B      | A       | Circle   | 3      | K2     |
#define KEY_BUTTON_R2   HID_KEY_X             // R2     | RT     | ZR      | R2       | 8      | K3     |
#define KEY_BUTTON_L2   HID_KEY_V             // L2     | LT     | ZL      | L2       | 7      | K4     |
#define KEY_BUTTON_B3   HID_KEY_CONTROL_LEFT  // B3     | X      | Y       | Square   | 1      | P1     |
#define KEY_BUTTON_B4   HID_KEY_ALT_LEFT      // B4     | Y      | X       | Triangle | 4      | P2     |
#define KEY_BUTTON_R1   HID_KEY_SPACE         // R1     | RB     | R       | R1       | 6      | P3     |
#define KEY_BUTTON_L1   HID_KEY_C             // L1     | LB     | L       | L1       | 5      | P4     |
#define KEY_BUTTON_S1   HID_KEY_5             // S1     | Back   | Minus   | Select   | 9      | Coin   |
#define KEY_BUTTON_S2   HID_KEY_1             // S2     | Start  | Plus    | Start    | 10     | Start  |
#define KEY_BUTTON_L3   HID_KEY_EQUAL         // L3     | LS     | LS      | L3       | 11     | LS     |
#define KEY_BUTTON_R3   HID_KEY_MINUS         // R3     | RS     | RS      | R3       | 12     | RS     |
#define KEY_BUTTON_A1   HID_KEY_9             // A1     | Guide  | Home    | PS       | 13     | ~      |
#define KEY_BUTTON_A2   HID_KEY_F2            // A2     | ~      | Capture | ~        | 14     | ~      |
#define KEY_BUTTON_FN   -1                    // Hotkey Function                                        |

// LED Configuration
#define BOARD_LEDS_PIN 16
#define LED_BRIGHTNESS_MAXIMUM 30
#define CASE_RGB_TYPE  CASE_RGB_TYPE_AMBIENT
#define CASE_RGB_INDEX 0
#define CASE_RGB_COUNT 1

// analog stick configuration
#define ANALOG_INPUT_ENABLED        1
#define ANALOG_ADC_1_VRX            27
#define ANALOG_ADC_1_VRY            26
#define ANALOG_ADC_2_VRX            29
#define ANALOG_ADC_2_VRY            28
#define DEFAULT_INNER_DEADZONE      0
#define DEFAULT_INNER_DEADZONE2     0


// Input Modes
#define DEFAULT_INPUT_MODE INPUT_MODE_PS4
#define DEFAULT_INPUT_MODE_B2 INPUT_MODE_XINPUT
#define DEFAULT_INPUT_MODE_B3 INPUT_MODE_PS4
#define DEFAULT_INPUT_MODE_B4 -1
#define DEFAULT_INPUT_MODE_B1 -1
#define DEFAULT_INPUT_MODE_R2 -1
#define DEFAULT_INPUT_MODE_L2 -1
#define DEFAULT_INPUT_MODE_R1 -1
#define DEFAULT_INPUT_MODE_L1 -1

// mini led
#define MINI_MENU_GAMEPAD_INPUT 1 
#define HAS_I2C_DISPLAY 1
#define I2C0_ENABLED 1
#define I2C0_PIN_SDA 8
#define I2C0_PIN_SCL 9
#define SPLASH_MODE SPLASH_MODE_STATIC
#define SPLASH_DURATION 1
#define DISPLAY_INVERT 3

// MINI LED layout
 #define BUTTON_LAYOUT BUTTON_LAYOUT_BOARD_DEFINED_A
 #define BUTTON_LAYOUT_RIGHT BUTTON_LAYOUT_BOARD_DEFINED_B

 // HML layout WASD--
 #define DEFAULT_BOARD_LAYOUT_A {\
    {GP_ELEMENT_DIR_BUTTON, {26, 28, 6, 3, 1, 1, GAMEPAD_MASK_UP, GP_SHAPE_POLYGON, 99}},\
    {GP_ELEMENT_DIR_BUTTON, {26, 56, 6, 3, 1, 1, GAMEPAD_MASK_DOWN, GP_SHAPE_POLYGON, 54}},\
    {GP_ELEMENT_DIR_BUTTON, {11, 42, 6, 3, 1, 1, GAMEPAD_MASK_LEFT, GP_SHAPE_POLYGON, 45}},\
    {GP_ELEMENT_DIR_BUTTON, {40, 42, 6, 3, 1, 1, GAMEPAD_MASK_RIGHT, GP_SHAPE_POLYGON, 90}},\
    {GP_ELEMENT_BTN_BUTTON, {48, 12, 51, 18, 1, 1, GAMEPAD_MASK_S1, GP_SHAPE_SQUARE}},\
    {GP_ELEMENT_BTN_BUTTON, {0, 10, 7, 14, 1, 1, GAMEPAD_MASK_L1, GP_SHAPE_SQUARE}},\
    {GP_ELEMENT_BTN_BUTTON, {0, 16, 7, 21, 1, 1, GAMEPAD_MASK_L2, GP_SHAPE_SQUARE}},\
    {GP_ELEMENT_BTN_BUTTON, {26, 42, 2, 6, 1, 1, GAMEPAD_MASK_L3, GP_SHAPE_POLYGON}},\
    {GP_ELEMENT_BTN_BUTTON, {54, 63, 63, 60, 1, 1, GAMEPAD_MASK_A2, GP_SHAPE_SQUARE}}\
}

// HML layout right--
#define DEFAULT_BOARD_LAYOUT_B {\
    {GP_ELEMENT_BTN_BUTTON, {100, 28, 6, 6, 1, 1, GAMEPAD_MASK_B4, GP_SHAPE_ELLIPSE}},\
    {GP_ELEMENT_BTN_BUTTON, {100, 56, 6, 6, 1, 1, GAMEPAD_MASK_B1, GP_SHAPE_ELLIPSE}},\
    {GP_ELEMENT_BTN_BUTTON, {86, 42, 6, 6, 1, 1, GAMEPAD_MASK_B3, GP_SHAPE_ELLIPSE}},\
    {GP_ELEMENT_BTN_BUTTON, {115, 42, 6, 6, 1, 1, GAMEPAD_MASK_B2, GP_SHAPE_ELLIPSE}},\
    {GP_ELEMENT_BTN_BUTTON, {75, 12, 78, 18, 1, 1, GAMEPAD_MASK_S2, GP_SHAPE_SQUARE}},\
    {GP_ELEMENT_BTN_BUTTON, {118, 10, 125, 14, 1, 1, GAMEPAD_MASK_R1, GP_SHAPE_SQUARE}},\
    {GP_ELEMENT_BTN_BUTTON, {118, 16, 125, 21, 1, 1, GAMEPAD_MASK_R2, GP_SHAPE_SQUARE}},\
    {GP_ELEMENT_BTN_BUTTON, {100, 42, 2, 6, 1, 1, GAMEPAD_MASK_R3, GP_SHAPE_POLYGON}},\
    {GP_ELEMENT_BTN_BUTTON, {63, 63, 72, 60, 1, 1, GAMEPAD_MASK_A2, GP_SHAPE_SQUARE}}\
}

#endif

