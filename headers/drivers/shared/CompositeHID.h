#pragma once

#include <stdint.h>
#include "tusb.h"

#define COMPOSITE_HID_KBD_REPORT_ID   0x01
#define COMPOSITE_HID_MOUSE_REPORT_ID 0x02

typedef struct __attribute__((packed, aligned(1))) {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycode[6];
} CompositeKeyboardReport;

typedef struct __attribute__((packed, aligned(1))) {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
} CompositeMouseReport;

static const uint8_t composite_hid_report_descriptor[] = {
    // Keyboard (Report ID 1)
    0x05, 0x01,                    // Usage Page (Generic Desktop)
    0x09, 0x06,                    // Usage (Keyboard)
    0xA1, 0x01,                    // Collection (Application)
    0x85, COMPOSITE_HID_KBD_REPORT_ID, //   Report ID (1)
    0x05, 0x07,                    //   Usage Page (Keyboard)
    0x19, 0xE0,                    //   Usage Minimum (Left Control)
    0x29, 0xE7,                    //   Usage Maximum (Right GUI)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0x01,                    //   Logical Maximum (1)
    0x75, 0x01,                    //   Report Size (1)
    0x95, 0x08,                    //   Report Count (8)
    0x81, 0x02,                    //   Input (Data,Var,Abs)
    0x95, 0x01,                    //   Report Count (1)
    0x75, 0x08,                    //   Report Size (8)
    0x81, 0x01,                    //   Input (Const,Ary,Abs)
    0x05, 0x08,                    //   Usage Page (LEDs)
    0x19, 0x01,                    //   Usage Minimum (Num Lock)
    0x29, 0x05,                    //   Usage Maximum (Kana)
    0x95, 0x05,                    //   Report Count (5)
    0x75, 0x01,                    //   Report Size (1)
    0x91, 0x02,                    //   Output (Data,Var,Abs)
    0x95, 0x01,                    //   Report Count (1)
    0x75, 0x03,                    //   Report Size (3)
    0x91, 0x01,                    //   Output (Const,Ary,Abs)
    0x05, 0x07,                    //   Usage Page (Keyboard)
    0x19, 0x00,                    //   Usage Minimum (0)
    0x2A, 0xFF, 0x00,              //   Usage Maximum (255)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x26, 0xFF, 0x00,              //   Logical Maximum (255)
    0x75, 0x08,                    //   Report Size (8)
    0x95, 0x06,                    //   Report Count (6)
    0x81, 0x00,                    //   Input (Data,Ary,Abs)
    0xC0,                          // End Collection

    // Mouse (Report ID 2): buttons(1) + x(1) + y(1) + wheel(1)
    0x05, 0x01,                    // Usage Page (Generic Desktop)
    0x09, 0x02,                    // Usage (Mouse)
    0xA1, 0x01,                    // Collection (Application)
    0x85, COMPOSITE_HID_MOUSE_REPORT_ID, //   Report ID (2)
    0x09, 0x01,                    //   Usage (Pointer)
    0xA1, 0x00,                    //   Collection (Physical)
    0x05, 0x09,                    //     Usage Page (Button)
    0x19, 0x01,                    //     Usage Minimum (Button 1)
    0x29, 0x03,                    //     Usage Maximum (Button 3)
    0x15, 0x00,                    //     Logical Minimum (0)
    0x25, 0x01,                    //     Logical Maximum (1)
    0x95, 0x03,                    //     Report Count (3)
    0x75, 0x01,                    //     Report Size (1)
    0x81, 0x02,                    //     Input (Data,Var,Abs)
    0x95, 0x01,                    //     Report Count (1)
    0x75, 0x05,                    //     Report Size (5)
    0x81, 0x01,                    //     Input (Const,Ary,Abs)
    0x05, 0x01,                    //     Usage Page (Generic Desktop)
    0x09, 0x30,                    //     Usage (X)
    0x09, 0x31,                    //     Usage (Y)
    0x09, 0x38,                    //     Usage (Wheel)
    0x15, 0x81,                    //     Logical Minimum (-127)
    0x25, 0x7F,                    //     Logical Maximum (127)
    0x75, 0x08,                    //     Report Size (8)
    0x95, 0x03,                    //     Report Count (3)
    0x81, 0x06,                    //     Input (Data,Var,Rel)
    0xC0,                          //   End Collection
    0xC0                           // End Collection
};
