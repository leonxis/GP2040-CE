#include "mode_manager.h"

#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mode_mgr";

#define APP_NVS_NAMESPACE "app"
#define APP_NVS_KEY_MODE  "mode"

static BleMode currentMode = BLE_MODE_XBOX;

// GP2040-CE input mode values (proto/enums.proto)
#define INPUT_MODE_XINPUT     0
#define INPUT_MODE_SWITCH     1
#define INPUT_MODE_PS4        4
#define INPUT_MODE_XBONE      5
#define INPUT_MODE_PS5        13
#define INPUT_MODE_SWITCH_PRO 15
#define INPUT_MODE_PS4B       17
#define INPUT_MODE_XINPUTB    18

bool mode_from_input_mode(uint8_t inputMode, BleMode *outMode) {
    switch (inputMode) {
        case INPUT_MODE_XINPUT:
        case INPUT_MODE_XINPUTB:
        case INPUT_MODE_XBONE:
            *outMode = BLE_MODE_XBOX;
            return true;
        case INPUT_MODE_SWITCH:
        case INPUT_MODE_SWITCH_PRO:
            *outMode = BLE_MODE_SWITCH;
            return true;
        case INPUT_MODE_PS4:
        case INPUT_MODE_PS4B:
        case INPUT_MODE_PS5:
            *outMode = BLE_MODE_DUALSENSE;
            return true;
        default:
            return false;
    }
}

BleMode mode_manager_load(void) {
    nvs_handle_t h;
    uint8_t stored = (uint8_t)BLE_MODE_XBOX;
    if (nvs_open(APP_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, APP_NVS_KEY_MODE, &stored);
        nvs_close(h);
    }

    switch ((BleMode)stored) {
        case BLE_MODE_XBOX:
        case BLE_MODE_SWITCH:
        case BLE_MODE_DUALSENSE:
            currentMode = (BleMode)stored;
            break;
        default:
            currentMode = BLE_MODE_XBOX;
            break;
    }
    return currentMode;
}

void mode_manager_save(BleMode mode) {
    nvs_handle_t h;
    if (nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, APP_NVS_KEY_MODE, (uint8_t)mode);
        nvs_commit(h);
        nvs_close(h);
    }
}

void mode_manager_request_switch(uint8_t inputMode) {
    BleMode requested;
    if (!mode_from_input_mode(inputMode, &requested)) {
        return;  // unmapped: ignore
    }
    if (requested == currentMode) {
        return;
    }
    ESP_LOGW(TAG, "switch BLE mode %d -> %d, rebooting",
             (int)currentMode, (int)requested);
    mode_manager_save(requested);
    vTaskDelay(pdMS_TO_TICKS(100));  // let NVS write settle
    esp_restart();
}

const char *mode_bond_namespace(BleMode mode) {
    switch (mode) {
        case BLE_MODE_SWITCH:    return "pro_bond";
        case BLE_MODE_DUALSENSE: return "ds_bond";
        case BLE_MODE_XBOX:
        default:                 return "xbox_bond";
    }
}

const char *mode_device_name(BleMode mode) {
    switch (mode) {
        case BLE_MODE_SWITCH:    return "MyProGamepad";
        case BLE_MODE_DUALSENSE: return "MyDSGamepad";
        case BLE_MODE_XBOX:
        default:                 return "MyXboxGamepad";
    }
}
