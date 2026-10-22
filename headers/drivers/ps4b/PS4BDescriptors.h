/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#pragma once

#include <stdint.h>
#include "drivers/ps4/PS4Descriptors.h"

// Configuration descriptor size: Config(9) + Gamepad(9+9+7+7)
#define PS4B_CONFIG1_DESC_SIZE		(9+9+9+7+7)
static const uint8_t ps4b_configuration_descriptor[] =
{
	// configuration descriptor, USB spec 9.6.3, page 264-266, Table 9-10
	9,						       // bLength;
	2,						       // bDescriptorType;
	LSB(PS4B_CONFIG1_DESC_SIZE),    // wTotalLength
	MSB(PS4B_CONFIG1_DESC_SIZE),
	1,	                           // bNumInterfaces (Gamepad only; Composite HID is appended by usbdriver)
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
	0x01                           // bInterval 1 (unit depends on device speed)
};

// PS4B-specific string descriptor
static const uint8_t ps4b_string_product[]      = "GP2040-CE (PS4B)";
