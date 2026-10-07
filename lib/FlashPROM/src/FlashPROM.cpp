/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2021 Jason Skuby (mytechtoybox.com)
 */

#include "FlashPROM.h"
#include "pico/assert.h"

uint8_t FlashPROM::writeCache[EEPROM_SIZE_BYTES];
volatile static alarm_id_t flashWriteAlarm = 0;
volatile static spin_lock_t *flashLock = nullptr;

static inline bool isMacroRecRangeValid(uint32_t offset, size_t len)
{
	return offset >= MACRO_REC_FLASH_OFFSET &&
	       len > 0 && len <= MACRO_REC_FLASH_SIZE &&
	       offset - MACRO_REC_FLASH_OFFSET <= MACRO_REC_FLASH_SIZE - len;
}

int64_t writeToFlash(alarm_id_t id, void *flashCache)
{
	while (is_spin_locked(flashLock));

	multicore_lockout_start_blocking();
	uint32_t interrupts = spin_lock_blocking(flashLock);

	flash_range_erase((intptr_t)EEPROM_ADDRESS_START - (intptr_t)XIP_BASE, EEPROM_SIZE_BYTES);
	flash_range_program((intptr_t)EEPROM_ADDRESS_START - (intptr_t)XIP_BASE, reinterpret_cast<uint8_t *>(flashCache), EEPROM_SIZE_BYTES);

	flashWriteAlarm = 0;

	multicore_lockout_end_blocking();
	spin_unlock(flashLock, interrupts);

	return 0;
}

void FlashPROM::eraseRange(uint32_t offset, size_t len)
{
	hard_assert(isMacroRecRangeValid(offset, len));
	hard_assert((offset & (FLASH_BLOCK_SIZE - 1)) == 0);
	hard_assert((len & (FLASH_BLOCK_SIZE - 1)) == 0);

	while (is_spin_locked(flashLock));

	multicore_lockout_start_blocking();
	uint32_t interrupts = spin_lock_blocking(flashLock);

	flash_range_erase(offset, len);

	multicore_lockout_end_blocking();
	spin_unlock(flashLock, interrupts);
}

void FlashPROM::programPages(uint32_t offset, const uint8_t * data, size_t len)
{
	hard_assert(isMacroRecRangeValid(offset, len));
	hard_assert((offset & (FLASH_PAGE_SIZE - 1)) == 0);
	hard_assert((len & (FLASH_PAGE_SIZE - 1)) == 0);

	while (is_spin_locked(flashLock));

	multicore_lockout_start_blocking();
	uint32_t interrupts = spin_lock_blocking(flashLock);

	flash_range_program(offset, data, len);

	multicore_lockout_end_blocking();
	spin_unlock(flashLock, interrupts);
}

void FlashPROM::start()
{
	if (flashLock == nullptr)
		flashLock = spin_lock_instance(spin_lock_claim_unused(true));

	memcpy(writeCache, reinterpret_cast<uint8_t *>(EEPROM_ADDRESS_START), EEPROM_SIZE_BYTES);
}

/* We don't have an actual EEPROM, so we need to be extra careful about minimizing writes. Instead
	of writing when a commit is requested, we update a time to actually commit. That way, if we receive multiple requests
	to commit in that timeframe, we'll hold off until the user is done sending changes. */
void FlashPROM::commit()
{
	while (is_spin_locked(flashLock));
	if (flashWriteAlarm != 0)
		cancel_alarm(flashWriteAlarm);
	flashWriteAlarm = add_alarm_in_ms(EEPROM_WRITE_WAIT, writeToFlash, writeCache, true);
}

void FlashPROM::reset()
{
	memset(writeCache, 0, EEPROM_SIZE_BYTES);
	commit();
}
