/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 Open Stick Community (gp2040-ce.info)
 */

#ifndef _USBDRIVER_CPP_
#define _USBDRIVER_CPP_

#include "usbdriver.h"
#include "tusb.h"
#include "drivermanager.h"
#include "storagemanager.h"
#include "config.pb.h"
#include "class/hid/hid.h"
#include "drivers/shared/CompositeHID.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include <string.h>

#define USB_CONFIG_DESC_COPY_SIZE 512
#define COMPOSITE_PATCH_BUF_SIZE  512
#define COMPOSITE_HID_EP_ADDR     0x87
#define COMPOSITE_HID_INTF_SIZE   25

static volatile bool usb_mounted;
static volatile bool usb_suspended;
static volatile bool usb_main_gamepad_report_armed;
static volatile uint32_t usb_main_gamepad_epoch;
static volatile uint32_t usb_main_gamepad_armed_epoch;
static volatile uint32_t usb_main_gamepad_sof_seq;
static volatile uint32_t usb_main_gamepad_sof_frame;
static volatile uint32_t usb_main_gamepad_sof_time_us;
static volatile uint32_t usb_main_gamepad_submit_seq;
static volatile uint32_t usb_main_gamepad_submit_time_us;
static volatile uint32_t usb_main_gamepad_complete_seq;
static volatile uint32_t usb_main_gamepad_complete_time_us;
static volatile uint32_t usb_main_gamepad_complete_sof_frame;
static volatile uint32_t usb_main_gamepad_complete_sof_time_us;
static volatile uint32_t usb_main_gamepad_failed_seq;
static volatile uint32_t usb_main_gamepad_failed_time_us;

static uint8_t compositeHIDInstance = 0xFF;

// Global variable to track current interface for get_report callback
// This is used by drivers to determine which interface is being queried
uint8_t current_hid_interface = 0;
static uint8_t patched_config_descriptor[COMPOSITE_PATCH_BUF_SIZE];
static CompositeKeyboardReport lastCompositeKeyboardReport = {};
static CompositeMouseReport lastCompositeMouseReport = {};

static bool shouldAddCompositeHID(InputMode mode) {
	return (mode == INPUT_MODE_XINPUTB) || (mode == INPUT_MODE_PS4B);
}

static uint8_t countHIDInterfaces(const uint8_t *descriptor, uint16_t total_len) {
	uint8_t count = 0;
	for (uint16_t pos = 0; pos + 2 <= total_len; ) {
		uint8_t len = descriptor[pos];
		if (len == 0 || pos + len > total_len) break;
		if (descriptor[pos + 1] == TUSB_DESC_INTERFACE && len >= 9 && descriptor[pos + 5] == TUSB_CLASS_HID) {
			count++;
		}
		pos += len;
	}
	return count;
}

static void buildCompositeHIDBlock(uint8_t *out, uint8_t interface_number) {
	out[0] = 9;  out[1] = TUSB_DESC_INTERFACE;
	out[2] = interface_number; out[3] = 0;
	out[4] = 1;  out[5] = TUSB_CLASS_HID; out[6] = 0; out[7] = 0; out[8] = 0;
	out[9] = 9;  out[10] = HID_DESC_TYPE_HID;
	out[11] = 0x11; out[12] = 0x01; out[13] = 0;
	out[14] = 1; out[15] = HID_DESC_TYPE_REPORT;
	out[16] = (uint8_t)(sizeof(composite_hid_report_descriptor) & 0xFF);
	out[17] = (uint8_t)((sizeof(composite_hid_report_descriptor) >> 8) & 0xFF);
	out[18] = 7; out[19] = TUSB_DESC_ENDPOINT;
	out[20] = COMPOSITE_HID_EP_ADDR; out[21] = TUSB_XFER_INTERRUPT;
	out[22] = 16; out[23] = 0; out[24] = 1;
}

static inline bool isCompositeInterface(uint8_t itf) {
	return (compositeHIDInstance != 0xFF) && (itf == compositeHIDInstance);
}

static inline void appendKeyIfPressed(bool pressed, uint8_t key, CompositeKeyboardReport *report, uint8_t *index) {
	if (pressed && *index < 6) {
		report->keycode[(*index)++] = key;
	}
}

void processCompositeHID(Gamepad *gamepad) {
	if (compositeHIDInstance == 0xFF || gamepad == nullptr) return;

	CompositeKeyboardReport keyboardReport = {};
	uint8_t keycodeIndex = 0;

	if (gamepad->pressedKeyboardKeyCtrl()) keyboardReport.modifier |= KEYBOARD_MODIFIER_LEFTCTRL;
	if (gamepad->pressedKeyboardKeyShift()) keyboardReport.modifier |= KEYBOARD_MODIFIER_LEFTSHIFT;
	if (gamepad->pressedKeyboardKeyAltF4()) {
		keyboardReport.modifier |= KEYBOARD_MODIFIER_LEFTALT;
		appendKeyIfPressed(true, HID_KEY_F4, &keyboardReport, &keycodeIndex);
	} else {
		appendKeyIfPressed(gamepad->pressedKeyboardKeyA(), HID_KEY_A, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyB(), HID_KEY_B, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyC(), HID_KEY_C, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyD(), HID_KEY_D, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyE(), HID_KEY_E, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyF(), HID_KEY_F, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyG(), HID_KEY_G, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyH(), HID_KEY_H, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyI(), HID_KEY_I, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyJ(), HID_KEY_J, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyK(), HID_KEY_K, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyL(), HID_KEY_L, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyM(), HID_KEY_M, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyN(), HID_KEY_N, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyO(), HID_KEY_O, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyP(), HID_KEY_P, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyQ(), HID_KEY_Q, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyR(), HID_KEY_R, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyS(), HID_KEY_S, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyT(), HID_KEY_T, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyU(), HID_KEY_U, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyV(), HID_KEY_V, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyW(), HID_KEY_W, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyX(), HID_KEY_X, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyY(), HID_KEY_Y, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKeyZ(), HID_KEY_Z, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKey0(), HID_KEY_0, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKey1(), HID_KEY_1, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKey2(), HID_KEY_2, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKey3(), HID_KEY_3, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKey4(), HID_KEY_4, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKey5(), HID_KEY_5, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKey6(), HID_KEY_6, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKey7(), HID_KEY_7, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKey8(), HID_KEY_8, &keyboardReport, &keycodeIndex);
		appendKeyIfPressed(gamepad->pressedKeyboardKey9(), HID_KEY_9, &keyboardReport, &keycodeIndex);
	}

	if ((memcmp(&lastCompositeKeyboardReport, &keyboardReport, sizeof(keyboardReport)) != 0) && tud_hid_n_ready(compositeHIDInstance)) {
		if (tud_hid_n_report(compositeHIDInstance, COMPOSITE_HID_KBD_REPORT_ID, &keyboardReport, sizeof(keyboardReport))) {
			lastCompositeKeyboardReport = keyboardReport;
		}
	}

	CompositeMouseReport mouseReport = {};
	mouseReport.buttons =
		(gamepad->pressedMouseLeft() ? 1 : 0) |
		(gamepad->pressedMouseRight() ? 2 : 0) |
		(gamepad->pressedMouseMiddle() ? 4 : 0);
	if (gamepad->auxState.sensors.mouse.enabled) {
		int16_t mx = gamepad->auxState.sensors.mouse.x;
		int16_t my = gamepad->auxState.sensors.mouse.y;
		if (mx > 127) mx = 127;
		else if (mx < -127) mx = -127;
		if (my > 127) my = 127;
		else if (my < -127) my = -127;
		mouseReport.x = (int8_t)mx;
		mouseReport.y = (int8_t)my;
	}
	mouseReport.wheel = 0;

	if ((memcmp(&lastCompositeMouseReport, &mouseReport, sizeof(mouseReport)) != 0) && tud_hid_n_ready(compositeHIDInstance)) {
		if (tud_hid_n_report(compositeHIDInstance, COMPOSITE_HID_MOUSE_REPORT_ID, &mouseReport, sizeof(mouseReport))) {
			lastCompositeMouseReport = mouseReport;
		}
	}
}

bool get_usb_mounted(void) {
	return usb_mounted;
}

bool get_usb_suspended(void) {
	return usb_suspended;
}

static void beginMainGamepadUSBEpoch(bool mounted) {
	uint32_t irqState = save_and_disable_interrupts();
	usb_mounted = mounted;
	usb_suspended = false;
	usb_main_gamepad_report_armed = false;
	usb_main_gamepad_armed_epoch = 0;
	usb_main_gamepad_sof_seq = 0;
	usb_main_gamepad_sof_frame = 0;
	usb_main_gamepad_sof_time_us = 0;
	usb_main_gamepad_submit_seq = 0;
	usb_main_gamepad_submit_time_us = 0;
	usb_main_gamepad_complete_seq = 0;
	usb_main_gamepad_complete_time_us = 0;
	usb_main_gamepad_complete_sof_frame = 0;
	usb_main_gamepad_complete_sof_time_us = 0;
	usb_main_gamepad_failed_seq = 0;
	usb_main_gamepad_failed_time_us = 0;
	usb_main_gamepad_epoch++;
	restore_interrupts(irqState);
}

void usb_get_main_gamepad_gate_snapshot(USBMainGamepadGateSnapshot* snapshot) {
	if (snapshot == nullptr) {
		return;
	}

	uint32_t irqState = save_and_disable_interrupts();
	snapshot->mounted = usb_mounted;
	snapshot->suspended = usb_suspended;
	snapshot->reportArmed =
		usb_main_gamepad_report_armed &&
		(usb_main_gamepad_armed_epoch == usb_main_gamepad_epoch);
	snapshot->epoch = usb_main_gamepad_epoch;
	snapshot->sofSeq = usb_main_gamepad_sof_seq;
	snapshot->sofFrame = usb_main_gamepad_sof_frame;
	snapshot->sofTimeUs = usb_main_gamepad_sof_time_us;
	snapshot->submitSeq = usb_main_gamepad_submit_seq;
	snapshot->submitTimeUs = usb_main_gamepad_submit_time_us;
	snapshot->completeSeq = usb_main_gamepad_complete_seq;
	snapshot->completeTimeUs = usb_main_gamepad_complete_time_us;
	snapshot->completeSofFrame = usb_main_gamepad_complete_sof_frame;
	snapshot->completeSofTimeUs = usb_main_gamepad_complete_sof_time_us;
	snapshot->failedSeq = usb_main_gamepad_failed_seq;
	snapshot->failedTimeUs = usb_main_gamepad_failed_time_us;
	restore_interrupts(irqState);
}

bool usb_mark_main_gamepad_report_submitted(uint32_t expectedEpoch) {
	uint32_t irqState = save_and_disable_interrupts();
	const bool canArm =
		usb_mounted &&
		!usb_suspended &&
		!usb_main_gamepad_report_armed &&
		(usb_main_gamepad_epoch == expectedEpoch);
	if (canArm) {
		usb_main_gamepad_armed_epoch = usb_main_gamepad_epoch;
		usb_main_gamepad_report_armed = true;
		usb_main_gamepad_submit_seq++;
		usb_main_gamepad_submit_time_us = time_us_32();
	}
	restore_interrupts(irqState);
	return canArm;
}

void usb_notify_main_gamepad_submit_failed(uint32_t expectedEpoch) {
	uint32_t irqState = save_and_disable_interrupts();
	if (usb_mounted &&
		!usb_suspended &&
		!usb_main_gamepad_report_armed &&
		(usb_main_gamepad_epoch == expectedEpoch)) {
		usb_main_gamepad_failed_seq++;
		usb_main_gamepad_failed_time_us = time_us_32();
	}
	restore_interrupts(irqState);
}

void usb_notify_main_gamepad_sof(uint32_t frameNumber) {
	uint32_t irqState = save_and_disable_interrupts();
	if (usb_mounted && !usb_suspended) {
		usb_main_gamepad_sof_frame = frameNumber;
		usb_main_gamepad_sof_time_us = time_us_32();
		usb_main_gamepad_sof_seq++;
	}
	restore_interrupts(irqState);
}

void usb_notify_main_gamepad_usb_reset(void) {
	beginMainGamepadUSBEpoch(false);
}

static void resetActiveDriverUSBState() {
        GPDriver* driver = DriverManager::getInstance().getDriver();
        if (driver != nullptr) {
                driver->onUSBReset();
        }
}

void usb_main_gamepad_hid_reset(uint8_t rhport) {
	hidd_reset(rhport);
        resetActiveDriverUSBState();
	usb_notify_main_gamepad_usb_reset();
}

void usb_notify_main_gamepad_poll_done_success(void) {
	uint32_t irqState = save_and_disable_interrupts();
	if (usb_mounted &&
		usb_main_gamepad_report_armed &&
		(usb_main_gamepad_armed_epoch == usb_main_gamepad_epoch)) {
		usb_main_gamepad_report_armed = false;
		usb_main_gamepad_complete_seq++;
		usb_main_gamepad_complete_time_us = time_us_32();
		usb_main_gamepad_complete_sof_frame =
			usb_main_gamepad_sof_frame;
		usb_main_gamepad_complete_sof_time_us =
			usb_main_gamepad_sof_time_us;
	}
	restore_interrupts(irqState);
}

void usb_notify_main_gamepad_poll_done_failed(void) {
	uint32_t irqState = save_and_disable_interrupts();
	if (usb_main_gamepad_report_armed &&
		(usb_main_gamepad_armed_epoch == usb_main_gamepad_epoch)) {
		usb_main_gamepad_report_armed = false;
		usb_main_gamepad_failed_seq++;
		usb_main_gamepad_failed_time_us = time_us_32();
	}
	restore_interrupts(irqState);
}

void usb_notify_main_gamepad_in_xfer_complete_from_xinput(void) {
	usb_notify_main_gamepad_poll_done_success();
}

void usb_notify_main_gamepad_in_xfer_failed_from_xinput(void) {
	usb_notify_main_gamepad_poll_done_failed();
}

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count) {
	*driver_count = 1;
	return DriverManager::getInstance().getDriver()->get_class_driver();
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
	if (isCompositeInterface(itf)) {
		memset(buffer, 0, reqlen);
		return reqlen;
	}
	// Store current interface number for get_report to use
	current_hid_interface = itf;
	return DriverManager::getInstance().getDriver()->get_report(report_id, report_type, buffer, reqlen);
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
	if (isCompositeInterface(itf)) {
		return;
	}
	DriverManager::getInstance().getDriver()->set_report(report_id, report_type, buffer, bufsize);
}

// Invoked when an IN report transfer is completed on HID endpoint.
// Track only gamepad HID interface instance (0) to avoid keyboard/mouse events
// perturbing main-loop cadence.
// XINPUT / XINPUTB: main gamepad uses vendor IN, not HID; do not count any HID
// completes (including composite keyboard/mouse) for the gate.
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len) {
	(void)report;
	(void)len;
	if (instance != 0) {
		return;
	}
	const InputMode mode = DriverManager::getInstance().getInputMode();
	if (mode == INPUT_MODE_XINPUT || mode == INPUT_MODE_XINPUTB) {
		return;
	}
	DriverManager::getInstance().getDriver()->onInputReportComplete();
	usb_notify_main_gamepad_poll_done_success();
}

void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t report_type, uint8_t const* report, uint16_t xferred_bytes) {
	(void)report;
	(void)xferred_bytes;
	if (instance != 0 || report_type != HID_REPORT_TYPE_INPUT) {
		return;
	}
	const InputMode mode = DriverManager::getInstance().getInputMode();
	if (mode == INPUT_MODE_XINPUT || mode == INPUT_MODE_XINPUTB) {
		return;
	}
	DriverManager::getInstance().getDriver()->onInputReportFailed();
	usb_notify_main_gamepad_poll_done_failed();
}

// Invoked when device is mounted
void tud_mount_cb(void)
{
	beginMainGamepadUSBEpoch(true);
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
	beginMainGamepadUSBEpoch(false);
        resetActiveDriverUSBState();
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
	(void)remote_wakeup_en;
	uint32_t irqState = save_and_disable_interrupts();
	usb_suspended = true;
	restore_interrupts(irqState);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
	uint32_t irqState = save_and_disable_interrupts();
	usb_suspended = false;
	restore_interrupts(irqState);
}

// Vendor Controlled XFER occured
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request) {
	return DriverManager::getInstance().getDriver()->vendor_control_xfer_cb(rhport, stage, request);
}


// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
	return DriverManager::getInstance().getDriver()->get_descriptor_string_cb(index, langid);
}

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb() {
	return DriverManager::getInstance().getDriver()->get_descriptor_device_cb();
}

// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf) {
	if (isCompositeInterface(itf)) {
		return composite_hid_report_descriptor;
	}
	return DriverManager::getInstance().getDriver()->get_hid_descriptor_report_cb(itf);
}

// HML: Copy configuration descriptor and patch bInterval for all IN endpoints from reportRate (device-level, all modes).
// Full-speed: bInterval 1=1ms(1kHz), 2=2ms(500Hz), 4=4ms(250Hz). Re-plug required after changing report rate.
static uint8_t usb_config_descriptor_copy[USB_CONFIG_DESC_COPY_SIZE];

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
	const uint8_t *raw = DriverManager::getInstance().getDriver()->get_descriptor_configuration_cb(index);
	if (!raw || raw[0] != 9 || raw[1] != 0x02) return raw;
	InputMode mode = DriverManager::getInstance().getInputMode();
	// NS Pro (Switch Pro) uses fixed 125Hz polling (bInterval=8) per the official protocol.
	// Do NOT patch bInterval for NS Pro — it breaks the handshake and causes the controller
	// to be recognized but unresponsive. Main branch returns the original descriptor directly.
	if (mode == INPUT_MODE_SWITCH_PRO) {
		compositeHIDInstance = 0xFF;
		return raw;
	}
	if (!shouldAddCompositeHID(mode)) {
		compositeHIDInstance = 0xFF;
	}
	uint16_t total_len = (uint16_t)raw[2] | ((uint16_t)raw[3] << 8);
	if (total_len > USB_CONFIG_DESC_COPY_SIZE) return raw;
	for (uint16_t i = 0; i < total_len; i++)
		usb_config_descriptor_copy[i] = raw[i];

	uint32_t rate_hz = Storage::getInstance().getAddonOptions().reportRate;
	uint8_t bint = 1;
	if (rate_hz == 250u) bint = 4;
	else if (rate_hz == 500u) bint = 2;

	for (uint16_t pos = 0; pos + 2 <= total_len; ) {
		uint8_t len = usb_config_descriptor_copy[pos];
		if (len == 0 || pos + len > total_len) break;
		uint8_t type = usb_config_descriptor_copy[pos + 1];
		if (type == 0x05 && len >= 7) {
			uint8_t addr = usb_config_descriptor_copy[pos + 2];
			if (addr & 0x80)
				usb_config_descriptor_copy[pos + 6] = bint;
		}
		pos += len;
	}
	if (!shouldAddCompositeHID(mode)) {
		return usb_config_descriptor_copy;
	}

	uint16_t newLen = total_len + COMPOSITE_HID_INTF_SIZE;
	if (newLen > COMPOSITE_PATCH_BUF_SIZE) {
		compositeHIDInstance = 0xFF;
		return usb_config_descriptor_copy;
	}

	memcpy(patched_config_descriptor, usb_config_descriptor_copy, total_len);
	patched_config_descriptor[2] = (uint8_t)(newLen & 0xFF);
	patched_config_descriptor[3] = (uint8_t)((newLen >> 8) & 0xFF);
	patched_config_descriptor[4] = (uint8_t)(patched_config_descriptor[4] + 1);
	compositeHIDInstance = countHIDInterfaces(usb_config_descriptor_copy, total_len);
	buildCompositeHIDBlock(&patched_config_descriptor[total_len], usb_config_descriptor_copy[4]);
	return patched_config_descriptor;
}

uint8_t const* tud_descriptor_device_qualifier_cb() {
	return DriverManager::getInstance().getDriver()->get_descriptor_device_qualifier_cb();
}

#endif
