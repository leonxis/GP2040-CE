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
#define GPIO_PIN_09 GpioAction::BUTTON_PRESS_UP     // UP     | UP     | UP      | UP       | UP     | UP     |
#define GPIO_PIN_06 GpioAction::BUTTON_PRESS_DOWN
#define GPIO_PIN_07 GpioAction::BUTTON_PRESS_LEFT   // LEFT   | LEFT   | LEFT    | LEFT     | LEFT   | LEFT   |
#define GPIO_PIN_08 GpioAction::BUTTON_PRESS_RIGHT  // RIGHT  | RIGHT  | RIGHT   | RIGHT    | RIGHT  | RIGHT  |
#define GPIO_PIN_05 GpioAction::BUTTON_PRESS_B1     // B1     | A      | B       | Cross    | 2      | K1     |
#define GPIO_PIN_04 GpioAction::BUTTON_PRESS_B2     // B2     | B      | A       | Circle   | 3      | K2     |
#define GPIO_PIN_00 GpioAction::BUTTON_PRESS_R2     // R2     | RT     | ZR      | R2       | 8      | K3     |
#define GPIO_PIN_01 GpioAction::BUTTON_PRESS_L2     // L2     | LT     | ZL      | L2       | 7      | K4     |
#define GPIO_PIN_02 GpioAction::BUTTON_PRESS_B3     // B3     | X      | Y       | Square   | 1      | P1     |
#define GPIO_PIN_03 GpioAction::BUTTON_PRESS_B4     // B4     | Y      | X       | Triangle | 4      | P2     |
#define GPIO_PIN_11 GpioAction::BUTTON_PRESS_A1     // A1     | Guide  | Home    | PS       | 13     | ~      |
#define GPIO_PIN_10 GpioAction::BUTTON_PRESS_FN 
// Setting GPIO pins to assigned by add-on
//
#define GPIO_PIN_14 GpioAction::ASSIGNED_TO_ADDON
#define GPIO_PIN_15 GpioAction::ASSIGNED_TO_ADDON
#define GPIO_PIN_26 GpioAction::ASSIGNED_TO_ADDON
#define GPIO_PIN_28 GpioAction::ASSIGNED_TO_ADDON
                                      |

// LED Configuration
#define BOARD_LEDS_PIN 16
#define LED_BRIGHTNESS_MAXIMUM 30
#define CASE_RGB_TYPE  CASE_RGB_TYPE_AMBIENT
#define CASE_RGB_INDEX 0
#define CASE_RGB_COUNT 1

// analog stick configuration
#define ANALOG_INPUT_ENABLED        1
#define ANALOG_ADC_1_VRX            28
#define ANALOG_ADC_1_VRY            26
#define DEFAULT_INNER_DEADZONE      0
#define DEFAULT_INNER_DEADZONE2     0
#define ANALOG_ADC_1_INVERT        INVERT_X
#define ANALOG_ADC_2_INVERT        INVERT_X


// Input Modes
#define DEFAULT_INPUT_MODE INPUT_MODE_PS4
#define DEFAULT_INPUT_MODE_B2 -1
#define DEFAULT_INPUT_MODE_B3 -1
#define DEFAULT_INPUT_MODE_B4 -1
#define DEFAULT_INPUT_MODE_B1 -1
#define DEFAULT_INPUT_MODE_R2 -1
#define DEFAULT_INPUT_MODE_L2 -1
#define DEFAULT_INPUT_MODE_R1 -1
#define DEFAULT_INPUT_MODE_L1 -1

// mini led
#define MINI_MENU_GAMEPAD_INPUT 1 
#define HAS_I2C_DISPLAY 1
#define I2C1_ENABLED 1
#define I2C1_PIN_SDA 14
#define I2C1_PIN_SCL 15
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

