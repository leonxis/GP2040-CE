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
#include "ws2812.h"

static const char *TAG = "main";

// 状态灯优先级（从高到低）：
//   红：未检测到手柄
//   蓝：检测到设备但无法识别模式/数据
//   黄：未连接到 nRF 接收端
//   绿：手柄 + nRF 链路正常
typedef enum {
    LED_STATE_RED = 0,
    LED_STATE_BLUE,
    LED_STATE_YELLOW,
    LED_STATE_GREEN,
} LedState;

static LedState eval_led_state(void) {
    bool dev_present = usb_gamepad_device_present();
    bool connected = usb_gamepad_connected();
    bool mode_ok = usb_gamepad_mode_recognized();
    bool link_ok = nrf24_link_ok();

    // 低频日志：每秒打印一次各条件，便于串口排查
    static int64_t last_log_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - last_log_us > 1000000) {
        last_log_us = now;
        ESP_LOGI(TAG, "led_eval: dev_present=%d connected=%d mode_ok=%d link_ok=%d",
                 dev_present, connected, mode_ok, link_ok);
    }

    if (!dev_present) {
        return LED_STATE_RED;
    }
    if (!connected || !mode_ok) {
        return LED_STATE_BLUE;
    }
    if (!link_ok) {
        return LED_STATE_YELLOW;
    }
    return LED_STATE_GREEN;
}

// 状态灯任务：常亮（绿）/闪烁（红蓝黄）
static void led_task(void *arg) {
    // 上电色序自检：R→G→B→Y→黑，每色 300ms，确认 LED 与 RMT 链路正常
    ws2812_set_rgb(255, 0, 0);   vTaskDelay(pdMS_TO_TICKS(300));
    ws2812_set_rgb(0, 255, 0);   vTaskDelay(pdMS_TO_TICKS(300));
    ws2812_set_rgb(0, 0, 255);   vTaskDelay(pdMS_TO_TICKS(300));
    ws2812_set_rgb(255, 200, 0); vTaskDelay(pdMS_TO_TICKS(300));
    ws2812_set_rgb(0, 0, 0);

    bool on = false;
    while (1) {
        LedState s = eval_led_state();
        if (s == LED_STATE_GREEN) {
            ws2812_set_rgb(0, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // 闪烁：250ms 亮 / 250ms 灭
        on = !on;
        if (on) {
            switch (s) {
                case LED_STATE_RED:    ws2812_set_rgb(255, 0, 0); break;
                case LED_STATE_BLUE:   ws2812_set_rgb(0, 0, 255); break;
                case LED_STATE_YELLOW: ws2812_set_rgb(255, 200, 0); break;
                default: break;
            }
        } else {
            ws2812_set_rgb(0, 0, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

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
    vTaskDelay(pdMS_TO_TICKS(10));

    // 初始化 WS2812 状态灯（上电即亮红灯自检）
    ESP_LOGI(TAG, "init ws2812...");
    vTaskDelay(pdMS_TO_TICKS(10));
    ws2812_init();

    // 先启动状态灯任务（优先级最低），确保即使后续初始化崩溃也能看到灯效
    ESP_LOGI(TAG, "create led_task...");
    vTaskDelay(pdMS_TO_TICKS(10));
    xTaskCreatePinnedToCore(led_task, "led", 4096, NULL, 1, NULL, 1);  // Core 1

    // 初始化 nRF24
    ESP_LOGI(TAG, "init nrf24...");
    vTaskDelay(pdMS_TO_TICKS(10));
    nrf24_init();

    // 初始化 USB Host
    ESP_LOGI(TAG, "init usb_host...");
    vTaskDelay(pdMS_TO_TICKS(10));
    usb_gamepad_init();

    // 启动转发任务
    ESP_LOGI(TAG, "create nrf24_fwd...");
    vTaskDelay(pdMS_TO_TICKS(10));
    xTaskCreatePinnedToCore(nrf24_forward_task, "nrf24_fwd", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Ready. Waiting for USB gamepad...");
}
