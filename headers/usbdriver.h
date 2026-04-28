#ifndef _USB_DRIVER_H_
#define _USB_DRIVER_H_

#include <stdint.h>

bool get_usb_mounted(void);
bool get_usb_suspended(void);

/** True while fewer than cold-start IN marks have been recorded; main loop bypasses gate. */
bool usb_main_loop_gate_usb_warmup_active(void);

/** Consume main-loop poll event marker (HID IN success / latched not-ready); returns true if was set. */
bool usb_consume_main_gamepad_poll_pending(void);

void usb_notify_main_gamepad_in_xfer_complete_from_xinput(void);
void usb_notify_main_gamepad_poll_done_success(void);
void usb_notify_main_gamepad_poll_done_not_ready(void);
void usb_reset_main_gamepad_poll_done_state(void);

#endif // #ifndef _USB_DRIVER_H_