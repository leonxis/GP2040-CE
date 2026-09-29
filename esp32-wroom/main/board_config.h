#pragma once

// ---------------------------------------------------------------------------
// Board configuration: ESP32-WROOM-32E
//
// UART link to RP2354 uses the native UART0 pins:
//   ESP32 GPIO1 (TXD0)  --> RP2354 GPIO19 (RX)
//   ESP32 GPIO3 (RXD0) <--  RP2354 GPIO18 (TX)
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
#define UART_SILENCE_TIMEOUT_MS 1000    // no bytes at all -> stop BLE
#define UART_BOOT_GRACE_MS      1000    // grace period after boot

// BLE report cadence (high-performance mode only)
#define BLE_SEND_PERIOD_MS      7
#define BLE_IDLE_POLL_MS        50

// Task priorities / cores / stacks
#define UART_RX_TASK_CORE       1
#define UART_RX_TASK_PRIO       10
#define UART_RX_TASK_STACK      4096

#define CTRL_TASK_CORE          0
#define CTRL_TASK_PRIO          8
#define CTRL_TASK_STACK         6144
