/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 Open Stick Community (gp2040-ce.info)
 */

#ifndef _USBDRIVER_CPP_
#define _USBDRIVER_CPP_

#include "tusb.h"
#include "drivermanager.h"
#include "storagemanager.h"
#include "config.pb.h"

#define USB_CONFIG_DESC_COPY_SIZE 512

static bool usb_mounted;
static bool usb_suspended;
static volatile uint32_t usb_sof_count = 0;
static volatile uint32_t usb_hid_gamepad_in_complete_count = 0;

// Global variable to track current interface for get_report callback
// This is used by drivers to determine which interface is being queried
uint8_t current_hid_interface = 0;

bool get_usb_mounted(void) {
	return usb_mounted;
}

bool get_usb_suspended(void) {
	return usb_suspended;
}

uint32_t get_usb_sof_count(void) {
	return usb_sof_count;
}

uint32_t get_usb_hid_gamepad_in_complete_count(void) {
	return usb_hid_gamepad_in_complete_count;
}

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count) {
	*driver_count = 1;
	return DriverManager::getInstance().getDriver()->get_class_driver();
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
	// Store current interface number for get_report to use
	current_hid_interface = itf;
	return DriverManager::getInstance().getDriver()->get_report(report_id, report_type, buffer, reqlen);
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
	DriverManager::getInstance().getDriver()->set_report(report_id, report_type, buffer, bufsize);
}

// Invoked when an IN report transfer is completed on HID endpoint.
// Track only gamepad HID interface instance (0) to avoid keyboard/mouse events
// perturbing main-loop cadence.
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len) {
	(void)report;
	(void)len;
	if (instance == 0) {
		usb_hid_gamepad_in_complete_count++;
	}
}

// Invoked every USB SOF (1ms on full-speed) when enabled by tud_sof_cb_enable(true).
void tud_sof_cb(uint32_t frame_count) {
	(void)frame_count;
	usb_sof_count++;
}

// Invoked when device is mounted
void tud_mount_cb(void)
{
	usb_mounted = true;
	usb_suspended = false;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
	usb_mounted = false;
	usb_suspended = false;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
	(void)remote_wakeup_en;
	usb_suspended = true;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
	usb_suspended = false;
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
	return DriverManager::getInstance().getDriver()->get_hid_descriptor_report_cb(itf);
}

// HML: Copy configuration descriptor and patch bInterval for all IN endpoints from reportRate (device-level, all modes).
// Full-speed: bInterval 1=1ms(1kHz), 2=2ms(500Hz), 4=4ms(250Hz). Re-plug required after changing report rate.
static uint8_t usb_config_descriptor_copy[USB_CONFIG_DESC_COPY_SIZE];

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
	const uint8_t *raw = DriverManager::getInstance().getDriver()->get_descriptor_configuration_cb(index);
	if (!raw || raw[0] != 9 || raw[1] != 0x02) return raw;
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
	return usb_config_descriptor_copy;
}

uint8_t const* tud_descriptor_device_qualifier_cb() {
	return DriverManager::getInstance().getDriver()->get_descriptor_device_qualifier_cb();
}

#endif
