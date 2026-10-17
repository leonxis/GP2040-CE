#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_config.h"
#include "gamepad_types.h"
#include "nrf24.h"
#include "usb_host_gamepad.h"
#include "report_parser.h"

static const char *TAG = "main";

// nRF24 转发任务：1ms 恒流发送，异步 ACK 轮询
static void nrf24_forward_task(void *arg) {
    uint32_t fail_count = 0;
    int64_t last_sent_us = 0;

    while (1) {
        // 阶段 1：在飞事务轮询 ACK
        int8_t tx_result = nrf24_pollTxComplete();
        if (tx_result >= 0) {
            if (tx_result == 1) {
                fail_count = 0;
            } else {
                fail_count++;
                if (fail_count >= NRF_FAIL_REINIT_THRESHOLD) {
                    ESP_LOGW(TAG, "nRF24 reinit after %" PRIu32 " failures", fail_count);
                    nrf24_reinit();
                    fail_count = 0;
                }
            }
        }

        // 阶段 2：芯片空闲时恒流发送
        int64_t now = esp_timer_get_time();
        if (tx_result != -1 && (now - last_sent_us) >= NRF_SEND_INTERVAL_US) {
            GamepadState state = {0};
            usb_gamepad_get_state(&state);

            GamepadMode mode = usb_gamepad_get_mode();
            uint8_t input_mode = INPUT_MODE_XINPUT;
            switch (mode) {
                case GP_MODE_SWITCH: input_mode = INPUT_MODE_SWITCH; break;
                case GP_MODE_PS4:    input_mode = INPUT_MODE_PS4;    break;
                case GP_MODE_XINPUT: input_mode = INPUT_MODE_XINPUT; break;
                default: break;
            }

            if (usb_gamepad_connected()) {
                nrf24_sendFrame(input_mode, state.buttons, state.dpad,
                                state.lx, state.ly, state.rx, state.ry,
                                state.lt, state.rt);
                last_sent_us = now;
            }
        }

        // 1ms 周期
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "ESP32-S3 USB Host -> nRF24 forwarder starting...");

    // 初始化 nRF24
    nrf24_init();

    // 初始化 USB Host
    usb_gamepad_init();

    // 启动转发任务
    xTaskCreatePinnedToCore(nrf24_forward_task, "nrf24_fwd", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Ready. Waiting for USB gamepad...");
}
