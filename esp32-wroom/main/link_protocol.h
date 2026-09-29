#pragma once

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Wire protocol shared with the RP2354 uart_link addon.
//
// Frame: 0xAA + version(=1) + type + len + payload[len] + CRC16(CCITT-FALSE)
// CRC is sent little-endian (crcLo, crcHi) and covers version..payload.
// ---------------------------------------------------------------------------

#define LP_MAGIC                  0xAA
#define LP_VERSION                1

#define LP_TYPE_INPUT             0x01
#define LP_TYPE_STATUS            0x03
#define LP_TYPE_LINK_STATUS       0x0B

#define LP_MAX_PAYLOAD            16

// INPUT frame payload (len=13)
typedef struct {
    uint16_t buttons;   // GP2040-CE lower-16 button mask
    uint8_t  dpad;      // bit0 up / bit1 down / bit2 left / bit3 right
    uint16_t lx, ly;    // 0..65535
    uint16_t rx, ry;    // 0..65535
    uint8_t  lt, rt;    // 0..255 linear triggers
} lp_input_t;

// STATUS frame payload (len=3)
typedef struct {
    uint8_t socdMode;
    uint8_t dpadMode;
    uint8_t inputMode;
} lp_status_t;

typedef void (*lp_input_cb_t)(const lp_input_t *in);
typedef void (*lp_status_cb_t)(const lp_status_t *st);
typedef void (*lp_activity_cb_t)(void);

typedef struct {
    uint8_t state;
    uint8_t type;
    uint8_t len;
    uint8_t idx;
    uint8_t payload[LP_MAX_PAYLOAD];
    uint16_t crcCalc;
    uint8_t crcLo;

    lp_input_cb_t    onInput;
    lp_status_cb_t   onStatus;
    lp_activity_cb_t onActivity;   // fired for EVERY received byte
} lp_parser_t;

void lp_parser_init(lp_parser_t *p,
                    lp_input_cb_t onInput,
                    lp_status_cb_t onStatus,
                    lp_activity_cb_t onActivity);

// Feed one received byte through the RX state machine.
void lp_feed(lp_parser_t *p, uint8_t b);

// Build a LINK_STATUS frame (type 0x0B, len=1) into buf; returns frame length.
size_t lp_build_link_status(uint8_t *buf, uint8_t bleConnected);

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
uint16_t lp_crc16_update(uint16_t crc, uint8_t b);
