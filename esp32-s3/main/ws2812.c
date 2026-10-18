#include "ws2812.h"
#include "board_config.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "ws2812";

static rmt_channel_handle_t s_chan = NULL;
static rmt_encoder_handle_t s_enc = NULL;

void ws2812_init(void) {
    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num = LED_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 80 * 1000 * 1000,   // 80MHz → 12.5ns/tick
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
    };
    if (rmt_new_tx_channel(&chan_cfg, &s_chan) != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed");
        return;
    }

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = {
            .level0 = 1, .duration0 = 28,   // T0H ≈ 350ns
            .level1 = 0, .duration1 = 72,   // T0L ≈ 900ns
        },
        .bit1 = {
            .level0 = 1, .duration0 = 72,   // T1H ≈ 900ns
            .level1 = 0, .duration1 = 28,   // T1L ≈ 350ns
        },
        .flags.msb_first = 1,
    };
    if (rmt_new_bytes_encoder(&enc_cfg, &s_enc) != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder failed");
        return;
    }

    if (rmt_enable(s_chan) != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed");
        return;
    }

    ESP_LOGI(TAG, "WS2812 ready on GPIO%d", LED_PIN);
    ws2812_set_rgb(255, 0, 0);  // 上电自检：红灯
}

void ws2812_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!s_chan || !s_enc) return;

    // 本板灯珠物理线序为 RGB（非标准 GRB）
    uint8_t data[3] = {r, g, b};

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };
    if (rmt_transmit(s_chan, s_enc, data, sizeof(data), &tx_cfg) != ESP_OK) {
        return;
    }
    // 等待发送完成：24bit × 1.25us ≈ 30us + 复位 ≥ 50us
    esp_rom_delay_us(100);
}
