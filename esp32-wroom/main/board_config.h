#pragma once

// ---------------------------------------------------------------------------
// Board configuration: ESP32-WROOM-32E transmitter for wireless-tx-2354 (BLE + nRF24)
//
// UART link to the Pico (RP2040/RP2354) uses the ESP32 native UART0 pins:
//   ESP32 GPIO1 (U0TXD, TX) --> Pico GPIO7 (RX)
//   ESP32 GPIO3 (U0RXD, RX) <-- Pico GPIO6 (TX)
// ---------------------------------------------------------------------------

#define LINK_UART_NUM           UART_NUM_0
#define LINK_UART_TX_GPIO       1
#define LINK_UART_RX_GPIO       3
#define LINK_UART_BAUD          921600
#define LINK_UART_RX_BUF_SIZE   1024
#define LINK_UART_TX_BUF_SIZE   256

// BOOT button (strapping pin): hold at power-on to erase per-mode bond data.
#define CLEAR_BONDS_GPIO        GPIO_NUM_0
#define CLEAR_BONDS_CONFIRM_MS  200

// Protocol timings (see link_protocol.h)
#define LINK_STATUS_PERIOD_MS   100     // send LINK_STATUS heartbeat
#define UART_SILENCE_TIMEOUT_MS 10000   // no CRC-valid frames for 10s -> disconnect BLE + sleep
#define UART_BOOT_GRACE_MS      1000    // grace period after boot

// BLE report cadence (high-performance mode only)
// Must be strictly greater than the BLE connection interval (7.5 ms).
// Otherwise the app produces notifications faster than the link can drain
// them one-per-event; the surplus queues up and the controller sends two
// packets in a single connection event, pushing the effective report rate
// above 133 Hz and causing intermittent disconnects.
#define BLE_SEND_PERIOD_MS      8
#define BLE_IDLE_POLL_MS        50

// ---------------------------------------------------------------------------
// nRF24L01+ link (SPI2). Active when STATUS radioMode=1; BLE sleeps then.
// Register config / payload layout / air parameters are aligned with the
// verified wireless-tx-2354 nrf24_link addon — receiver depends on them.
// ESP32-WROOM-32E: GPIO16..19 and GPIO4 are free (no PSRAM on WROOM).
// ---------------------------------------------------------------------------
#define NRF24_CE_GPIO           GPIO_NUM_19
#define NRF24_CSN_GPIO          18
#define NRF24_SCK_GPIO          17
#define NRF24_MOSI_GPIO         16
#define NRF24_MISO_GPIO         4
#define NRF24_SPI_CLOCK_HZ      8000000 // 8MHz / MODE0 / MSB
#define NRF24_CHANNEL           84      // 2.484 GHz
#define NRF24_PAYLOAD_LEN       15      // [0]=inputMode [1]=seq + 13B input
#define NRF24_ADDR_BYTES        { 0xE7, 0xE7, 0xE7, 0xE7, 0xE7 }

// Async TX: CE pulse triggers one packet (datasheet: >10us), result polled later
#define NRF24_CE_PULSE_US       15
// SETUP_RETR 0x03 (ARD=250us, ARC=3) bounds a transaction to ~1.14 ms;
// 2.5 ms without TX_DS/MAX_RT means chip misbehaved -> flush + recover
#define NRF24_TX_TIMEOUT_US     2500
// Send cadence comes from the Pico side: it pushes one UART INPUT frame every
// 900us and cb_input forwards each immediately (no local rate limit).
// Consecutive TX failures before reinitializing the chip (watchdog)
#define NRF24_FAIL_REINIT_THRESHOLD 500
// No ACK for this long -> receiver considered offline
#define NRF24_LINK_DOWN_TIMEOUT_US  1000000

// Task priorities / cores / stacks
#define UART_RX_TASK_CORE       1
#define UART_RX_TASK_PRIO       10
#define UART_RX_TASK_STACK      4096

#define CTRL_TASK_CORE          0
#define CTRL_TASK_PRIO          8
#define CTRL_TASK_STACK         6144
