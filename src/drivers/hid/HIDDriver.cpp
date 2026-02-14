/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#include "drivers/hid/HIDDriver.h"
#include "drivers/hid/HIDDescriptors.h"
#include "drivers/shared/driverhelper.h"
#include "storagemanager.h"
#include "class/hid/hid.h"
#include <string.h>

static bool hid_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
	return hidd_control_xfer_cb(rhport, stage, request);
}

void HIDDriver::initialize() {
	hidReport = {
		.buttons = 0,
		.direction = HID_HAT_NOTHING,
		.l_x_axis = HID_JOYSTICK_MID, .l_y_axis = HID_JOYSTICK_MID,
		.r_x_axis = HID_JOYSTICK_MID, .r_y_axis = HID_JOYSTICK_MID,
	};
	
	// Initialize keyboard report (report_id is handled by TinyUSB, not in data)
	keyboardReport = {
		.report_id = HID_REPORT_ID_KEYBOARD, // Used for reference only, not sent in data
		.modifier = 0,
		.reserved = 0,
		.keycode = {0, 0, 0, 0, 0, 0}
	};

	class_driver = {
	#if CFG_TUSB_DEBUG >= 2
		.name = "HID",
	#endif
		.init = hidd_init,
		.reset = hidd_reset,
		.open = hidd_open,
		.control_xfer_cb = hid_control_xfer_cb,
		.xfer_cb = hidd_xfer_cb,
		.sof = NULL
	};
}

// Generate HID report from gamepad and send to TUSB Device
bool HIDDriver::process(Gamepad * gamepad) {
	switch (gamepad->state.dpad & GAMEPAD_MASK_DPAD)
	{
		case GAMEPAD_MASK_UP:                        hidReport.direction = HID_HAT_UP;        break;
		case GAMEPAD_MASK_UP | GAMEPAD_MASK_RIGHT:   hidReport.direction = HID_HAT_UPRIGHT;   break;
		case GAMEPAD_MASK_RIGHT:                     hidReport.direction = HID_HAT_RIGHT;     break;
		case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT: hidReport.direction = HID_HAT_DOWNRIGHT; break;
		case GAMEPAD_MASK_DOWN:                      hidReport.direction = HID_HAT_DOWN;      break;
		case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT:  hidReport.direction = HID_HAT_DOWNLEFT;  break;
		case GAMEPAD_MASK_LEFT:                      hidReport.direction = HID_HAT_LEFT;      break;
		case GAMEPAD_MASK_UP | GAMEPAD_MASK_LEFT:    hidReport.direction = HID_HAT_UPLEFT;    break;
		default:                                     hidReport.direction = HID_HAT_NOTHING;   break;
	}

	hidReport.l_x_axis = static_cast<uint8_t>(gamepad->state.lx >> 8);
	hidReport.l_y_axis = static_cast<uint8_t>(gamepad->state.ly >> 8);
	hidReport.r_x_axis = static_cast<uint8_t>(gamepad->state.rx >> 8);
	hidReport.r_y_axis = static_cast<uint8_t>(gamepad->state.ry >> 8);

	// these first three buttons are in this unintuitive order to be compatible with
	// expectations, e.g. both PS3/4/5 modes and Switch modes map to HID as
	// B3 B4  ==  1 4
	// B1 B2  ==  2 3
	hidReport.buttons = 0
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
	keyboardReport.modifier = 0;
	memset(keyboardReport.keycode, 0, 6);
	uint8_t keycodeIndex = 0;
	
	// Check for modifier keys
	if (gamepad->pressedKeyboardKeyCtrl()) {
		keyboardReport.modifier |= KEYBOARD_MODIFIER_LEFTCTRL;
	}
	if (gamepad->pressedKeyboardKeyShift()) {
		keyboardReport.modifier |= KEYBOARD_MODIFIER_LEFTSHIFT;
	}
	
	// Check for Alt+F4 combination
	if (gamepad->pressedKeyboardKeyAltF4()) {
		keyboardReport.modifier |= KEYBOARD_MODIFIER_LEFTALT;
		if (keycodeIndex < 6) {
			keyboardReport.keycode[keycodeIndex++] = HID_KEY_F4;
		}
	} else {
		// Check for letter keys
		if (gamepad->pressedKeyboardKeyA() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_A;
		if (gamepad->pressedKeyboardKeyB() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_B;
		if (gamepad->pressedKeyboardKeyC() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_C;
		if (gamepad->pressedKeyboardKeyD() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_D;
		if (gamepad->pressedKeyboardKeyE() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_E;
		if (gamepad->pressedKeyboardKeyF() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_F;
		if (gamepad->pressedKeyboardKeyG() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_G;
		if (gamepad->pressedKeyboardKeyH() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_H;
		if (gamepad->pressedKeyboardKeyI() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_I;
		if (gamepad->pressedKeyboardKeyJ() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_J;
		if (gamepad->pressedKeyboardKeyK() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_K;
		if (gamepad->pressedKeyboardKeyL() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_L;
		if (gamepad->pressedKeyboardKeyM() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_M;
		if (gamepad->pressedKeyboardKeyN() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_N;
		if (gamepad->pressedKeyboardKeyO() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_O;
		if (gamepad->pressedKeyboardKeyP() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_P;
		if (gamepad->pressedKeyboardKeyQ() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_Q;
		if (gamepad->pressedKeyboardKeyR() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_R;
		if (gamepad->pressedKeyboardKeyS() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_S;
		if (gamepad->pressedKeyboardKeyT() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_T;
		if (gamepad->pressedKeyboardKeyU() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_U;
		if (gamepad->pressedKeyboardKeyV() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_V;
		if (gamepad->pressedKeyboardKeyW() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_W;
		if (gamepad->pressedKeyboardKeyX() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_X;
		if (gamepad->pressedKeyboardKeyY() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_Y;
		if (gamepad->pressedKeyboardKeyZ() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_Z;
		if (gamepad->pressedKeyboardKey0() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_0;
		if (gamepad->pressedKeyboardKey1() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_1;
		if (gamepad->pressedKeyboardKey2() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_2;
		if (gamepad->pressedKeyboardKey3() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_3;
		if (gamepad->pressedKeyboardKey4() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_4;
		if (gamepad->pressedKeyboardKey5() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_5;
		if (gamepad->pressedKeyboardKey6() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_6;
		if (gamepad->pressedKeyboardKey7() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_7;
		if (gamepad->pressedKeyboardKey8() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_8;
		if (gamepad->pressedKeyboardKey9() && keycodeIndex < 6) keyboardReport.keycode[keycodeIndex++] = HID_KEY_9;
	}

	// Send gamepad report
	void * report = &hidReport;
	uint16_t report_size = sizeof(hidReport);
	bool gamepadSent = false;
	if (memcmp(last_report, report, report_size) != 0)
	{
		// HID ready + report sent, copy previous report
		if (tud_hid_ready() && tud_hid_report(HID_REPORT_ID_GAMEPAD, report, report_size) == true ) {
			memcpy(last_report, report, report_size);
			gamepadSent = true;
		}
	}
	
	// Send keyboard report
	// Note: TinyUSB automatically prepends report_id to the data, so we send the report without report_id field
	uint8_t kb_report_data[8]; // modifier(1) + reserved(1) + keycode[6] = 8 bytes
	kb_report_data[0] = keyboardReport.modifier;
	kb_report_data[1] = keyboardReport.reserved;
	memcpy(&kb_report_data[2], keyboardReport.keycode, 6);
	uint16_t kb_report_size = 8;
	bool keyboardSent = false;
	if (memcmp(last_keyboard_report, kb_report_data, kb_report_size) != 0)
	{
		if (tud_hid_ready() && tud_hid_report(HID_REPORT_ID_KEYBOARD, kb_report_data, kb_report_size) == true ) {
			memcpy(last_keyboard_report, kb_report_data, kb_report_size);
			keyboardSent = true;
		}
	}
	
	return gamepadSent || keyboardSent;
}

// tud_hid_get_report_cb
uint16_t HIDDriver::get_report(uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
	if (report_id == HID_REPORT_ID_GAMEPAD) {
		memcpy(buffer, &hidReport, sizeof(HIDReport));
		return sizeof(HIDReport);
	} else if (report_id == HID_REPORT_ID_KEYBOARD) {
		// Return keyboard report data without report_id (TinyUSB handles report_id separately)
		buffer[0] = keyboardReport.modifier;
		buffer[1] = keyboardReport.reserved;
		memcpy(&buffer[2], keyboardReport.keycode, 6);
		return 8; // modifier(1) + reserved(1) + keycode[6] = 8 bytes
	}
	return 0;
}

// Only PS4 does anything with set report
void HIDDriver::set_report(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {}

// Only XboxOG and Xbox One use vendor control xfer cb
bool HIDDriver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
	return false;
}

const uint16_t * HIDDriver::get_descriptor_string_cb(uint8_t index, uint16_t langid) {
    char *value;
    // Check for override settings
    GamepadOptions & gamepadOptions = Storage::getInstance().getGamepadOptions();
    if ( gamepadOptions.usbDescOverride == true ) {
        switch(index) {
            case 1:
                value = gamepadOptions.usbDescManufacturer;
                break;
            case 2:
                value = gamepadOptions.usbDescProduct;
                break;
            case 3:
                value = gamepadOptions.usbDescVersion;
            default:
                value = (char *)hid_string_descriptors[index];
                break;
        }
    } else {
        value = (char *)hid_string_descriptors[index];
    }

	return getStringDescriptor(value, index); // getStringDescriptor returns a static array
}

const uint8_t * HIDDriver::get_descriptor_device_cb() {
    // Check for override settings
    GamepadOptions & gamepadOptions = Storage::getInstance().getGamepadOptions();
    if ( gamepadOptions.usbOverrideID == true ) {
        static uint8_t modified_device_descriptor[18];
        memcpy(modified_device_descriptor, hid_device_descriptor, sizeof(hid_device_descriptor));
        memcpy(&modified_device_descriptor[8], (uint8_t*)&gamepadOptions.usbVendorID, sizeof(uint16_t)); // Vendor ID
        memcpy(&modified_device_descriptor[10], (uint8_t*)&gamepadOptions.usbProductID, sizeof(uint16_t)); // Product ID
        return (const uint8_t*)modified_device_descriptor;
    }

	return hid_device_descriptor;
}

const uint8_t * HIDDriver::get_hid_descriptor_report_cb(uint8_t itf) {
	return hid_report_descriptor;
}

const uint8_t * HIDDriver::get_descriptor_configuration_cb(uint8_t index) {
	return hid_configuration_descriptor;
}

const uint8_t * HIDDriver::get_descriptor_device_qualifier_cb() {
	return nullptr;
}

uint16_t HIDDriver::GetJoystickMidValue() {
	return HID_JOYSTICK_MID << 8;
}
