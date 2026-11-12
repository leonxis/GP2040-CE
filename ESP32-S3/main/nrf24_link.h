#pragma once

#include <stdint.h>
#include <stdbool.h>

// Minimal nRF24L01+ TX driver, register-for-register aligned with the
// verified wireless-tx-2354 nrf24_link addon (RP2354 side).
// On-air payload: 15 bytes — [0]=inputMode [1]=txSeq [2..3]=buttons(LE)
// [4]=dpad [5..12]=lx/ly/rx/ry(LE) [13]=lt [14]=rt.
// The receiver parses this exact layout; do NOT change field order/width.
// All calls happen on the controller task (Core0).

// Initialize SPI bus + CE/CSN pins. Chip stays in Power Down afterwards.
void nrf24_link_init(void);

// (Re)configure registers and enter TX mode (PWR_UP). Idempotent.
void nrf24_link_start(void);

// Power Down + CE low. Idempotent.
void nrf24_link_sleep(void);

// Compose the 15-byte frame and submit it asynchronously (CE pulse).
// A frame still in flight is left untouched (skip, constant-stream semantics).
void nrf24_link_send_frame(uint8_t inputMode, uint16_t buttons, uint8_t dpad,
                           uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry,
                           uint8_t lt, uint8_t rt);

// Advance the async TX state machine: harvest TX_DS/MAX_RT, software-timeout
// recovery, consecutive-failure watchdog reinit. Call every loop iteration.
void nrf24_link_poll(void);

// True when an ACK was seen within the last 1 s (receiver online).
bool nrf24_link_ok(void);
