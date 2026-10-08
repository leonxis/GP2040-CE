/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2021 Jason Skuby (mytechtoybox.com)
 */

#ifndef FLASHPROM_H_
#define FLASHPROM_H_

#include <stdint.h>
#include <string.h>
#include <pico/lock_core.h>
#include <pico/multicore.h>
#include <hardware/flash.h>
#include <hardware/timer.h>

#define EEPROM_SIZE_BYTES    0x8000           // Reserve 32k of flash memory (ensure this value is divisible by 256)
#define EEPROM_ADDRESS_START _u(0x101F8000) // The arduino-pico EEPROM lib starts here, so we'll do the same

// Warning: If the write wait is too long it can stall other processes
#define EEPROM_WRITE_WAIT    50             // Amount of time in ms to wait before blocking core1 and committing to flash

// Dedicated macro-recording area: 384KB (6 x 64KB blocks, 64KB aligned) directly
// below the config region, holding two 192KB recorded-macro slots (macro 1/2).
// Firmware image must stay below this offset (build check).
#define MACRO_REC_FLASH_OFFSET _u(0x190000)
#define MACRO_REC_FLASH_SIZE   _u(0x60000)

class FlashPROM
{
	public:
		void start();
		void commit();
		void reset();

		// Erase a 64KB-aligned range inside the macro-recording area. Callers are
		// blocked for the whole erase (RP2350 ROM issues 0xD8 block erases).
		static void eraseRange(uint32_t offset, size_t len);

		// Program whole 256B pages inside the macro-recording area (offset/len page aligned).
		static void programPages(uint32_t offset, const uint8_t * data, size_t len);

		static uint8_t writeCache[EEPROM_SIZE_BYTES];
};

inline FlashPROM EEPROM;

#endif
