#pragma once

#include <stdint.h>

// BLE device identities exposed by the firmware.
typedef enum {
    BLE_MODE_XBOX = 0,
    BLE_MODE_SWITCH = 1,
    BLE_MODE_DUALSENSE = 3,   // skip 2 (matches upstream multi-host scheme)
} BleMode;

// Map a GP2040-CE inputMode to a BLE mode.
//   xbox:      XINPUT(0), XINPUTB(18), XBONE(5)
//   switch:    SWITCH(1), SWITCH_PRO(15)
//   dualsense: PS4(4), PS4B(17), PS5(13)
// Returns true on a hit; false means the inputMode is unsupported (ignored).
bool mode_from_input_mode(uint8_t inputMode, BleMode *outMode);

// Load the startup mode from NVS (namespace "app", key "mode").
// Defaults to BLE_MODE_XBOX when unset/invalid.
BleMode mode_manager_load(void);

// Persist a mode as the startup identity.
void mode_manager_save(BleMode mode);

// If inputMode maps to a mode different from the currently running one,
// persist it and reboot. Unmapped inputModes are ignored.
void mode_manager_request_switch(uint8_t inputMode);

// --- per-mode identity data ---

// NimBLE bond NVS namespace: "xbox_bond" / "pro_bond" / "ds_bond"
const char *mode_bond_namespace(BleMode mode);

// Advertising/device name.
const char *mode_device_name(BleMode mode);
