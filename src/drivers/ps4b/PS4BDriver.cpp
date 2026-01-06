/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#include "drivers/ps4b/PS4BDriver.h"
#include "drivers/shared/driverhelper.h"
#include "storagemanager.h"
#include "class/hid/hid.h"
#include <string.h>

extern uint8_t current_hid_interface; // Declared in usbdriver.cpp

static bool ps4b_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
	return hidd_control_xfer_cb(rhport, stage, request);
}

void PS4BDriver::initialize() {
	// Initialize gamepad report
	gamepadReport = {
		.buttons = 0,
		.direction = HID_HAT_NOTHING,
		.l_x_axis = HID_JOYSTICK_MID,
		.l_y_axis = HID_JOYSTICK_MID,
		.r_x_axis = HID_JOYSTICK_MID,
		.r_y_axis = HID_JOYSTICK_MID,
	};
	
	// Initialize keyboard report
	keyboard_modifier = 0;
	keyboard_reserved = 0;
	memset(keyboard_keycode, 0, 6);

	// No class driver registration - let Windows use built-in HID driver
	// class_driver is not used for PS4B mode
}

bool PS4BDriver::process(Gamepad * gamepad) {
	// Convert D-pad to HID hat
	switch (gamepad->state.dpad & GAMEPAD_MASK_DPAD)
	{
		case GAMEPAD_MASK_UP:                        gamepadReport.direction = HID_HAT_UP;        break;
		case GAMEPAD_MASK_UP | GAMEPAD_MASK_RIGHT:   gamepadReport.direction = HID_HAT_UPRIGHT;   break;
		case GAMEPAD_MASK_RIGHT:                     gamepadReport.direction = HID_HAT_RIGHT;     break;
		case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT: gamepadReport.direction = HID_HAT_DOWNRIGHT; break;
		case GAMEPAD_MASK_DOWN:                      gamepadReport.direction = HID_HAT_DOWN;      break;
		case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT:  gamepadReport.direction = HID_HAT_DOWNLEFT;  break;
		case GAMEPAD_MASK_LEFT:                      gamepadReport.direction = HID_HAT_LEFT;      break;
		case GAMEPAD_MASK_UP | GAMEPAD_MASK_LEFT:    gamepadReport.direction = HID_HAT_UPLEFT;    break;
		default:                                     gamepadReport.direction = HID_HAT_NOTHING;   break;
	}

	// Convert analog sticks
	gamepadReport.l_x_axis = static_cast<uint8_t>(gamepad->state.lx >> 8);
	gamepadReport.l_y_axis = static_cast<uint8_t>(gamepad->state.ly >> 8);
	gamepadReport.r_x_axis = static_cast<uint8_t>(gamepad->state.rx >> 8);
	gamepadReport.r_y_axis = static_cast<uint8_t>(gamepad->state.ry >> 8);

	// Convert buttons (same mapping as HIDDriver)
	gamepadReport.buttons = 0
		| (gamepad->pressedB1()    ? GAMEPAD_MASK_B2     : 0)
		| (gamepad->pressedB2()    ? GAMEPAD_MASK_B3     : 0)
		| (gamepad->pressedB3()    ? GAMEPAD_MASK_B1     : 0)
		| (gamepad->pressedB4()    ? GAMEPAD_MASK_B4     : 0)
		| (gamepad->pressedL1()    ? GAMEPAD_MASK_L1     : 0)
		| (gamepad->pressedR1()    ? GAMEPAD_MASK_R1     : 0)
		| (gamepad->pressedL2()    ? GAMEPAD_MASK_L2     : 0)
		| (gamepad->pressedR2()    ? GAMEPAD_MASK_R2     : 0)
		| (gamepad->pressedS1()    ? GAMEPAD_MASK_S1     : 0)
		| (gamepad->pressedS2()    ? GAMEPAD_MASK_S2     : 0)
		| (gamepad->pressedL3()    ? GAMEPAD_MASK_L3     : 0)
		| (gamepad->pressedR3()    ? GAMEPAD_MASK_R3     : 0)
		| (gamepad->pressedA1()    ? GAMEPAD_MASK_A1     : 0)
		| (gamepad->pressedA2()    ? GAMEPAD_MASK_A2     : 0)
		| (gamepad->pressedA3()    ? GAMEPAD_MASK_A3     : 0)
		| (gamepad->pressedA4()    ? GAMEPAD_MASK_A4     : 0)
		| (gamepad->pressedUp()    ? GAMEPAD_MASK_DU     : 0)
		| (gamepad->pressedDown()  ? GAMEPAD_MASK_DD     : 0)
		| (gamepad->pressedLeft()  ? GAMEPAD_MASK_DL     : 0)
		| (gamepad->pressedRight() ? GAMEPAD_MASK_DR     : 0)
		| (gamepad->pressedE1()    ? GAMEPAD_MASK_E1     : 0)
		| (gamepad->pressedE2()    ? GAMEPAD_MASK_E2     : 0)
		| (gamepad->pressedE3()    ? GAMEPAD_MASK_E3     : 0)
		| (gamepad->pressedE4()    ? GAMEPAD_MASK_E4     : 0)
		| (gamepad->pressedE5()    ? GAMEPAD_MASK_E5     : 0)
		| (gamepad->pressedE6()    ? GAMEPAD_MASK_E6     : 0)
		| (gamepad->pressedE7()    ? GAMEPAD_MASK_E7     : 0)
		| (gamepad->pressedE8()    ? GAMEPAD_MASK_E8     : 0)
		| (gamepad->pressedE9()    ? GAMEPAD_MASK_E9     : 0)
		| (gamepad->pressedE10()   ? GAMEPAD_MASK_E10    : 0)
		| (gamepad->pressedE11()   ? GAMEPAD_MASK_E11    : 0)
		| (gamepad->pressedE12()   ? GAMEPAD_MASK_E12    : 0)
	;

	// Wake up TinyUSB device
	if (tud_suspended())
		tud_remote_wakeup();

	// Process keyboard report
	keyboard_modifier = 0;
	memset(keyboard_keycode, 0, 6);
	uint8_t keycodeIndex = 0;
	
	// Check for modifier keys
	if (gamepad->pressedKeyboardKeyCtrl()) {
		keyboard_modifier |= KEYBOARD_MODIFIER_LEFTCTRL;
	}
	if (gamepad->pressedKeyboardKeyShift()) {
		keyboard_modifier |= KEYBOARD_MODIFIER_LEFTSHIFT;
	}
	
	// Check for Alt+F4 combination
	if (gamepad->pressedKeyboardKeyAltF4()) {
		keyboard_modifier |= KEYBOARD_MODIFIER_LEFTALT;
		if (keycodeIndex < 6) {
			keyboard_keycode[keycodeIndex++] = HID_KEY_F4;
		}
	} else {
		// Check for letter keys
		if (gamepad->pressedKeyboardKeyA() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_A;
		if (gamepad->pressedKeyboardKeyB() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_B;
		if (gamepad->pressedKeyboardKeyC() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_C;
		if (gamepad->pressedKeyboardKeyD() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_D;
		if (gamepad->pressedKeyboardKeyE() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_E;
		if (gamepad->pressedKeyboardKeyF() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_F;
		if (gamepad->pressedKeyboardKeyG() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_G;
		if (gamepad->pressedKeyboardKeyH() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_H;
		if (gamepad->pressedKeyboardKeyI() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_I;
		if (gamepad->pressedKeyboardKeyJ() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_J;
		if (gamepad->pressedKeyboardKeyK() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_K;
		if (gamepad->pressedKeyboardKeyL() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_L;
		if (gamepad->pressedKeyboardKeyM() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_M;
		if (gamepad->pressedKeyboardKeyN() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_N;
		if (gamepad->pressedKeyboardKeyO() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_O;
		if (gamepad->pressedKeyboardKeyP() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_P;
		if (gamepad->pressedKeyboardKeyQ() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_Q;
		if (gamepad->pressedKeyboardKeyR() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_R;
		if (gamepad->pressedKeyboardKeyS() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_S;
		if (gamepad->pressedKeyboardKeyT() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_T;
		if (gamepad->pressedKeyboardKeyU() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_U;
		if (gamepad->pressedKeyboardKeyV() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_V;
		if (gamepad->pressedKeyboardKeyW() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_W;
		if (gamepad->pressedKeyboardKeyX() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_X;
		if (gamepad->pressedKeyboardKeyY() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_Y;
		if (gamepad->pressedKeyboardKeyZ() && keycodeIndex < 6) keyboard_keycode[keycodeIndex++] = HID_KEY_Z;
	}

	// Send gamepad report (Interface 0, no report ID for single-report interface)
	uint16_t gamepad_report_size = sizeof(gamepadReport);
	bool gamepadSent = false;
	if (memcmp(last_gamepad_report, &gamepadReport, gamepad_report_size) != 0)
	{
		// Use tud_hid_n_ready and tud_hid_n_report for specific interface
		if (tud_hid_n_ready(GAMEPAD_INTERFACE) && tud_hid_n_report(GAMEPAD_INTERFACE, 0, &gamepadReport, gamepad_report_size) == true ) {
			memcpy(last_gamepad_report, &gamepadReport, gamepad_report_size);
			gamepadSent = true;
		}
	}
	
	// Send keyboard report (Interface 1, no report ID for single-report interface)
	uint8_t kb_report_data[8]; // modifier(1) + reserved(1) + keycode[6] = 8 bytes
	kb_report_data[0] = keyboard_modifier;
	kb_report_data[1] = keyboard_reserved;
	memcpy(&kb_report_data[2], keyboard_keycode, 6);
	uint16_t kb_report_size = 8;
	bool keyboardSent = false;
	if (memcmp(last_keyboard_report, kb_report_data, kb_report_size) != 0)
	{
		// Use tud_hid_n_ready and tud_hid_n_report for specific interface
		if (tud_hid_n_ready(KEYBOARD_INTERFACE) && tud_hid_n_report(KEYBOARD_INTERFACE, 0, kb_report_data, kb_report_size) == true ) {
			memcpy(last_keyboard_report, kb_report_data, kb_report_size);
			keyboardSent = true;
		}
	}
	
	return gamepadSent || keyboardSent;
}

uint16_t PS4BDriver::get_report(uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
	if (report_type != HID_REPORT_TYPE_FEATURE) {
		if (current_hid_interface == GAMEPAD_INTERFACE) {
			memcpy(buffer, &gamepadReport, sizeof(gamepadReport));
			return sizeof(gamepadReport);
		} else if (current_hid_interface == KEYBOARD_INTERFACE) {
			buffer[0] = keyboard_modifier;
			buffer[1] = keyboard_reserved;
			memcpy(&buffer[2], keyboard_keycode, 6);
			return 8; // modifier(1) + reserved(1) + keycode[6] = 8 bytes
		}
	}
	return 0;
}

void PS4BDriver::set_report(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
	// No-op for PS4B mode
}

bool PS4BDriver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
	return false;
}

const uint16_t * PS4BDriver::get_descriptor_string_cb(uint8_t index, uint16_t langid) {
	const char *value = (const char *)ps4b_string_descriptors[index];
	return getStringDescriptor(value, index);
}

const uint8_t * PS4BDriver::get_descriptor_device_cb() {
	return ps4b_device_descriptor;
}

const uint8_t * PS4BDriver::get_hid_descriptor_report_cb(uint8_t itf) {
	if (itf == GAMEPAD_INTERFACE) {
		return ps4b_gamepad_report_descriptor;
	} else if (itf == KEYBOARD_INTERFACE) {
		return ps4b_keyboard_report_descriptor;
	}
	return nullptr;
}

const uint8_t * PS4BDriver::get_descriptor_configuration_cb(uint8_t index) {
	return ps4b_configuration_descriptor;
}

const uint8_t * PS4BDriver::get_descriptor_device_qualifier_cb() {
	return nullptr;
}

uint16_t PS4BDriver::GetJoystickMidValue() {
	return HID_JOYSTICK_MID << 8;
}
