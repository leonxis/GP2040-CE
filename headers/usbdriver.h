#ifndef _USB_DRIVER_H_
#define _USB_DRIVER_H_

#include <stdint.h>

struct USBMainGamepadGateSnapshot {
	bool mounted;
	bool suspended;
	bool reportArmed;
	uint32_t epoch;
	uint32_t sofSeq;
	uint32_t sofFrame;
	uint32_t sofTimeUs;
	uint32_t submitSeq;
	uint32_t submitTimeUs;
	uint32_t completeSeq;
	uint32_t completeTimeUs;
	uint32_t completeSofFrame;
	uint32_t completeSofTimeUs;
	uint32_t failedSeq;
	uint32_t failedTimeUs;
};

bool get_usb_mounted(void);
bool get_usb_suspended(void);

void usb_get_main_gamepad_gate_snapshot(USBMainGamepadGateSnapshot* snapshot);
bool usb_mark_main_gamepad_report_submitted(uint32_t expectedEpoch);
void usb_notify_main_gamepad_submit_failed(uint32_t expectedEpoch);
void usb_notify_main_gamepad_sof(uint32_t frameNumber);
void usb_notify_main_gamepad_usb_reset(void);
void usb_main_gamepad_hid_reset(uint8_t rhport);

void usb_notify_main_gamepad_in_xfer_complete_from_xinput(void);
void usb_notify_main_gamepad_in_xfer_failed_from_xinput(void);
void usb_notify_main_gamepad_poll_done_success(void);
void usb_notify_main_gamepad_poll_done_failed(void);

#endif // #ifndef _USB_DRIVER_H_
