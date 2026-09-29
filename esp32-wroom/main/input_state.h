#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "link_protocol.h"

// Latest complete controller state, published by the UART RX task (Core1)
// and consumed by the BLE controller task (Core0).

typedef struct {
    uint16_t buttons;
    uint8_t  dpad;      // bit0 up / bit1 down / bit2 left / bit3 right
    uint16_t lx, ly;
    uint16_t rx, ry;
    uint8_t  lt, rt;
} InputSnapshot;

class InputState {
public:
    void init() {
        spinlock = portMUX_INITIALIZER_UNLOCKED;
        reset();
    }

    void reset() {
        taskENTER_CRITICAL(&spinlock);
        snap.buttons = 0;
        snap.dpad = 0;
        snap.lx = snap.ly = snap.rx = snap.ry = 0x8000;
        snap.lt = snap.rt = 0;
        taskEXIT_CRITICAL(&spinlock);
    }

    void publish(const lp_input_t *in) {
        taskENTER_CRITICAL(&spinlock);
        snap.buttons = in->buttons;
        snap.dpad = in->dpad;
        snap.lx = in->lx;
        snap.ly = in->ly;
        snap.rx = in->rx;
        snap.ry = in->ry;
        snap.lt = in->lt;
        snap.rt = in->rt;
        taskEXIT_CRITICAL(&spinlock);
    }

    void take(InputSnapshot *out) {
        taskENTER_CRITICAL(&spinlock);
        *out = snap;
        taskEXIT_CRITICAL(&spinlock);
    }

private:
    portMUX_TYPE spinlock;
    InputSnapshot snap;
};

// Global shared instances (defined in main.cpp)
extern InputState g_input;

// Tick of the last byte received from the RP2354 (any byte, frame-valid or
// not). 32-bit aligned volatile access is atomic on ESP32.
extern volatile TickType_t g_uartLastActiveTick;
