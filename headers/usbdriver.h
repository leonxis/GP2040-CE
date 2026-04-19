#ifndef _USB_DRIVER_H_
#define _USB_DRIVER_H_

#include <stdint.h>

bool get_usb_mounted(void);
bool get_usb_suspended(void);

uint32_t get_usb_sof_count(void);
uint32_t get_usb_main_gamepad_in_complete_count(void);
uint32_t get_usb_hid_gamepad_in_complete_count(void);

void usb_notify_main_gamepad_in_xfer_complete_from_xinput(void);

#endif // #ifndef _USB_DRIVER_H_