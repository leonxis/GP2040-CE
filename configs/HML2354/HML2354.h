/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * HML2354 board header - RP2354B (QFN-80, 48 GPIO, 2MB stacked flash)
 * Derived from Raspberry Pi RP2350B board header conventions.
 */

#ifndef _BOARDS_HML2354_H
#define _BOARDS_HML2354_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// For board detection
#define HML2354

// --- RP2350 VARIANT ---
// 0 = RP2350B/RP2354B (48 GPIO, QFN-80)
#define PICO_RP2350A 0

// --- FLASH ---
// RP2354B: 2MB stacked flash on the QSPI bus
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

#endif
