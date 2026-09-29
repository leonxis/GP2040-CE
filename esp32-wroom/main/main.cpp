#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "board_config.h"
#include "link_protocol.h"
#include "input_state.h"
#include "mode_manager.h"
#include "IMyGamepad.h"
#include "XboxOneGamepad.h"
#include "SwitchProController.h"
#include "DualSenseController.h"

// Per-mode bond NVS namespace, consumed by the forked NimBLE bt component.
#include "../components/bt/nimble_nvs_namespace_variable.h"
const char *NIMBLE_NVS_NAMESPACE;

InputState g_input;
volatile TickType_t g_uartLastActiveTick = 0;

static const char *TAG = "main";
static BleMode s_mode;
static IMyGamepad *s_pad = nullptr;

// ---------------------------------------------------------------------------
// Frame parser callbacks (run on the UART RX task, Core1)
// ---------------------------------------------------------------------------

static void cb_input(const lp_input_t *in) {
    g_input.publish(in);
}

static void cb_status(const lp_status_t *st) {
    // Unmapped inputModes are ignored; mapped changes persist and reboot.
    mode_manager_request_switch(st->inputMode);
}

static void cb_activity(void) {
    g_uartLastActiveTick = xTaskGetTickCount();
}

// ---------------------------------------------------------------------------
// Clear-bonds: hold BOOT (GPIO0) at power-on
// ---------------------------------------------------------------------------

static bool clear_bonds_button_held() {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << CLEAR_BONDS_GPIO;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);

    bool first = gpio_get_level(CLEAR_BONDS_GPIO) == 0;
    vTaskDelay(pdMS_TO_TICKS(CLEAR_BONDS_CONFIRM_MS));
    bool second = gpio_get_level(CLEAR_BONDS_GPIO) == 0;
    return first && second;
}

// IDF 5.1 has no public API to erase a single NVS namespace, so a full NVS
// wipe is used. NimBLE bond data lives there too; the selected BLE mode is
// written back immediately afterwards so the identity is not lost.
static void wipe_all_nvs_preserving_mode(BleMode mode) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
    mode_manager_save(mode);
    ESP_LOGW(TAG, "all bond data erased; BLE mode preserved");
}

// ---------------------------------------------------------------------------
// UART link setup
// ---------------------------------------------------------------------------

static void setup_uart() {
    uart_config_t cfg = {};
    cfg.baud_rate = LINK_UART_BAUD;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_param_config(LINK_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART_NUM,
                                LINK_UART_TX_GPIO, LINK_UART_RX_GPIO,
                                UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART_NUM,
                                        LINK_UART_RX_BUF_SIZE,
                                        LINK_UART_TX_BUF_SIZE,
                                        0, nullptr, 0));
}

static void uart_rx_task(void *arg) {
    (void)arg;
    lp_parser_t parser;
    lp_parser_init(&parser, cb_input, cb_status, cb_activity);

    uint8_t byte;
    while (true) {
        if (uart_read_bytes(LINK_UART_NUM, &byte, 1, portMAX_DELAY) == 1) {
            lp_feed(&parser, byte);
        }
    }
}

// ---------------------------------------------------------------------------
// BLE controller task (Core0; every NimBLE call happens here)
// ---------------------------------------------------------------------------

static IMyGamepad *create_pad(BleMode mode) {
    const char *name = mode_device_name(mode);
    switch (mode) {
        case BLE_MODE_SWITCH:    return new SwitchProController(name);
        case BLE_MODE_DUALSENSE: return new DualSenseController(name);
        case BLE_MODE_XBOX:
        default:                 return new XboxOneGamepad(name);
    }
}

static void controller_task(void *arg) {
    (void)arg;

    s_pad = create_pad(s_mode);
    s_pad->start();
    bool bleRunning = true;

    const TickType_t bootTick = xTaskGetTickCount();
    TickType_t lastSend = xTaskGetTickCount();
    TickType_t lastStatus = 0;

    while (true) {
        const TickType_t now = xTaskGetTickCount();

        const bool afterGrace =
            (now - bootTick) >= pdMS_TO_TICKS(UART_BOOT_GRACE_MS);
        const bool linkAlive =
            !afterGrace ||
            (now - g_uartLastActiveTick) < pdMS_TO_TICKS(UART_SILENCE_TIMEOUT_MS);

        if (linkAlive && !bleRunning) {
            s_pad->start();
            bleRunning = true;
        } else if (!linkAlive && bleRunning) {
            s_pad->stop();
            bleRunning = false;
        }

        // LINK_STATUS heartbeat, always — this is also the Pico's online check.
        if (now - lastStatus >= pdMS_TO_TICKS(LINK_STATUS_PERIOD_MS)) {
            uint8_t frame[7];
            const bool connected =
                bleRunning && s_pad->getConnectedCount() > 0;
            const size_t len =
                lp_build_link_status(frame, connected ? 1 : 0);
            uart_write_bytes(LINK_UART_NUM, frame, len);
            lastStatus = now;
        }

        if (bleRunning && s_pad->getConnectedCount() > 0) {
            InputSnapshot snap;
            g_input.take(&snap);
            s_pad->update(&snap);
            vTaskDelayUntil(&lastSend, pdMS_TO_TICKS(BLE_SEND_PERIOD_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(BLE_IDLE_POLL_MS));
        }
    }
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------

extern "C" void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_mode = mode_manager_load();

    if (clear_bonds_button_held()) {
        wipe_all_nvs_preserving_mode(s_mode);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    NIMBLE_NVS_NAMESPACE = mode_bond_namespace(s_mode);

    // Per-mode BLE identity: base MAC from efuse, offset by mode. Must be set
    // before NimBLE initializes.
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    mac[1] = (uint8_t)(mac[1] + (uint8_t)s_mode);
    ESP_ERROR_CHECK(esp_iface_mac_addr_set(mac, ESP_MAC_BT));

    setup_uart();

    g_input.init();
    g_uartLastActiveTick = xTaskGetTickCount();  // boot grace: assume alive

    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx", UART_RX_TASK_STACK,
                            nullptr, UART_RX_TASK_PRIO, nullptr,
                            UART_RX_TASK_CORE);
    xTaskCreatePinnedToCore(controller_task, "ctrl", CTRL_TASK_STACK,
                            nullptr, CTRL_TASK_PRIO, nullptr,
                            CTRL_TASK_CORE);
}
