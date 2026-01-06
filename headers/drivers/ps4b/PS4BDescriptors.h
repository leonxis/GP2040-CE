/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#pragma once

#include <stdint.h>
#include "drivers/ps4/PS4Descriptors.h"  // Include PS4Descriptors for shared structs

// PS4B-specific interface and endpoint definitions
#define KEYBOARD_INTERFACE	1 // Keyboard interface number
#define KEYBOARD_ENDPOINT	2 // Keyboard endpoint number
#define KEYBOARD_SIZE		8 // Keyboard report size

// Keyboard Report Descriptor for PS4B composite device
static const uint8_t ps4b_keyboard_report_descriptor[] =
{
	0x05, 0x01,        // USAGE_PAGE (Generic Desktop)
	0x09, 0x06,        // USAGE (Keyboard)
	0xa1, 0x01,        // COLLECTION (Application)
	// Modifier keys (8 bits)
	0x05, 0x07,        //   USAGE_PAGE (Keyboard)
	0x19, 0xe0,        //   USAGE_MINIMUM (Left Control)
	0x29, 0xe7,        //   USAGE_MAXIMUM (Right GUI)
	0x15, 0x00,        //   LOGICAL_MINIMUM (0)
	0x25, 0x01,        //   LOGICAL_MAXIMUM (1)
	0x75, 0x01,        //   REPORT_SIZE (1)
	0x95, 0x08,        //   REPORT_COUNT (8)
	0x81, 0x02,        //   INPUT (Data,Var,Abs)
	// Reserved byte
	0x95, 0x01,        //   REPORT_COUNT (1)
	0x75, 0x08,        //   REPORT_SIZE (8)
	0x81, 0x01,        //   INPUT (Cnst,Ary,Abs)
	// LED output (5 bits)
	0x05, 0x08,        //   USAGE_PAGE (LEDs)
	0x19, 0x01,        //   USAGE_MINIMUM (Num Lock)
	0x29, 0x05,        //   USAGE_MAXIMUM (Kana)
	0x95, 0x05,        //   REPORT_COUNT (5)
	0x75, 0x01,        //   REPORT_SIZE (1)
	0x91, 0x02,        //   OUTPUT (Data,Var,Abs)
	// LED padding (3 bits)
	0x95, 0x01,        //   REPORT_COUNT (1)
	0x75, 0x03,        //   REPORT_SIZE (3)
	0x91, 0x01,        //   OUTPUT (Cnst,Ary,Abs)
	// Keycodes (6 bytes)
	0x05, 0x07,        //   USAGE_PAGE (Keyboard)
	0x19, 0x00,        //   USAGE_MINIMUM (Reserved)
	0x2A, 0xff, 0x00,  //   USAGE_MAXIMUM_N (255, 2 bytes, little-endian)
	0x15, 0x00,        //   LOGICAL_MINIMUM (0)
	0x26, 0xff, 0x00,  //   LOGICAL_MAXIMUM (255)
	0x75, 0x08,        //   REPORT_SIZE (8)
	0x95, 0x06,        //   REPORT_COUNT (6)
	0x81, 0x00,        //   INPUT (Data,Ary,Abs)
	0xc0               // END_COLLECTION
};

// Configuration descriptor size: Config(9) + Gamepad Interface(9) + Gamepad HID(9) + Gamepad EP IN(7) + Gamepad EP OUT(7) + Keyboard Interface(9) + Keyboard HID(9) + Keyboard EP(7)
#define PS4B_CONFIG1_DESC_SIZE		(9+9+9+7+7+9+9+7)
static const uint8_t ps4b_configuration_descriptor[] =
{
	// configuration descriptor, USB spec 9.6.3, page 264-266, Table 9-10
	9,						       // bLength;
	2,						       // bDescriptorType;
	LSB(PS4B_CONFIG1_DESC_SIZE),    // wTotalLength
	MSB(PS4B_CONFIG1_DESC_SIZE),
	2,	                           // bNumInterfaces (Gamepad + Keyboard)
	1,	                           // bConfigurationValue
	0,	                           // iConfiguration
	0x80,                          // bmAttributes
	50,	                           // bMaxPower
		// interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
	9,				               // bLength
	4,				               // bDescriptorType
	GAMEPAD_INTERFACE,             // bInterfaceNumber
	0,				               // bAlternateSetting
	2,				               // bNumEndpoints
	0x03,			               // bInterfaceClass (0x03 = HID)
	0x00,			               // bInterfaceSubClass (0x00 = No Boot)
	0x00,			               // bInterfaceProtocol (0x00 = No Protocol)
	0,				               // iInterface
		// HID interface descriptor, HID 1.11 spec, section 6.2.1
	9,							   // bLength
	0x21,						   // bDescriptorType
	0x11, 0x01,					   // bcdHID
	0,							   // bCountryCode
	1,							   // bNumDescriptors
	0x22,						   // bDescriptorType
	LSB(sizeof(ps4_report_descriptor)), // wDescriptorLength
	MSB(sizeof(ps4_report_descriptor)),
		// endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
	7,						 	   // bLength
	5,						       // bDescriptorType
	GAMEPAD_ENDPOINT | 0x80,       // bEndpointAddress
	0x03,					       // bmAttributes (0x03=intr)
	GAMEPAD_SIZE, 0,		       // wMaxPacketSize
	1,						       // bInterval (1 ms)
	0x07,                          // bLength
	0x05,                          // bDescriptorType (Endpoint)
	0x03,                          // bEndpointAddress (OUT/H2D)
	0x03,                          // bmAttributes (Interrupt)
	0x40, 0x00,                    // wMaxPacketSize 64
	0x01,                          // bInterval 1 (unit depends on device speed)
	
	// Keyboard Interface (Interface 1)
	// interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
	9,				               // bLength
	4,				               // bDescriptorType
	KEYBOARD_INTERFACE,            // bInterfaceNumber
	0,				               // bAlternateSetting
	1,				               // bNumEndpoints
	0x03,			               // bInterfaceClass (0x03 = HID)
	0x00,			               // bInterfaceSubClass (0x00 = No Boot)
	0x01,			               // bInterfaceProtocol (0x01 = Keyboard)
	0,				               // iInterface
	// HID interface descriptor, HID 1.11 spec, section 6.2.1
	9,							   // bLength
	0x21,						   // bDescriptorType
	0x11, 0x01,					   // bcdHID
	0,							   // bCountryCode
	1,							   // bNumDescriptors
	0x22,						   // bDescriptorType
	LSB(sizeof(ps4b_keyboard_report_descriptor)), // wDescriptorLength
	MSB(sizeof(ps4b_keyboard_report_descriptor)),
	// endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
	7,						 	   // bLength
	5,						       // bDescriptorType
	KEYBOARD_ENDPOINT | 0x80,      // bEndpointAddress
	0x03,					       // bmAttributes (0x03=intr)
	KEYBOARD_SIZE, 0,		       // wMaxPacketSize
	1						       // bInterval (1 ms)
};

// PS4B-specific string descriptor
static const uint8_t ps4b_string_product[]      = "GP2040-CE (PS4B)";
