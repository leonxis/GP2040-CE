#include "nrf24.h"
#include "board_config.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

static const char *TAG = "nrf24";

static spi_device_handle_t spi_dev = NULL;
static bool spi_inited = false;
static uint8_t txSeq = 0;
static bool txPending = false;
static int64_t txStartUs = 0;
static volatile int64_t s_last_ack_us = 0;

#define NRF_LINK_TIMEOUT_US  500000  // 500ms 未收到 ACK 视为链路断开

// ---- SPI 寄存器原语 ----
static void writeReg(uint8_t reg, uint8_t val) {
    uint8_t tx[2] = {0x20 | reg, val};
    spi_transaction_t t = {.length = 16, .tx_buffer = tx};
    spi_device_polling_transmit(spi_dev, &t);
}

static void writeRegBuf(uint8_t reg, const uint8_t *data, uint8_t len) {
    uint8_t tx[32];
    tx[0] = 0x20 | reg;
    for (uint8_t i = 0; i < len; i++) tx[1 + i] = data[i];
    spi_transaction_t t = {.length = (len + 1) * 8, .tx_buffer = tx};
    spi_device_polling_transmit(spi_dev, &t);
}

static uint8_t readReg(uint8_t reg) {
    uint8_t tx[2] = {reg, 0};
    uint8_t rx[2] = {0};
    spi_transaction_t t = {.length = 16, .tx_buffer = tx, .rx_buffer = rx};
    spi_device_polling_transmit(spi_dev, &t);
    return rx[1];
}

static void flushTx(void) {
    uint8_t tx = NRF_CMD_FLUSH_TX;
    spi_transaction_t t = {.length = 8, .tx_buffer = &tx};
    spi_device_polling_transmit(spi_dev, &t);
}

static void flushRx(void) {
    uint8_t tx = NRF_CMD_FLUSH_RX;
    spi_transaction_t t = {.length = 8, .tx_buffer = &tx};
    spi_device_polling_transmit(spi_dev, &t);
}

static void cePulse(uint32_t us) {
    gpio_set_level(NRF_PIN_CE, 1);
    esp_rom_delay_us(us);
    gpio_set_level(NRF_PIN_CE, 0);
}

// ---- 公共 API ----

// nRF24 寄存器配置（与 nrf24_link.cpp::setup() 一致）
static void nrf24_config_regs(void) {
    writeReg(NRF_REG_CONFIG, 0x00);            // power down
    writeReg(NRF_REG_EN_AA, 0x01);             // EN_AA: pipe0 auto-ack
    writeReg(NRF_REG_EN_RXADDR, 0x01);         // EN_RXADDR: pipe0 only
    writeReg(NRF_REG_SETUP_AW, 0x03);          // 5-byte addresses
    writeReg(NRF_REG_SETUP_RETR, 0x03);        // 250us, 3 retries
    writeReg(NRF_REG_RF_CH, NRF_CHANNEL);      // RF_CH
    writeReg(NRF_REG_RF_SETUP, 0x0E);          // 2Mbps, 0dBm
    static const uint8_t addr[5] = {NRF_ADDR_B0, NRF_ADDR_B1, NRF_ADDR_B2,
                                    NRF_ADDR_B3, NRF_ADDR_B4};
    writeRegBuf(NRF_REG_TX_ADDR, addr, 5);
    writeRegBuf(NRF_REG_RX_ADDR_P0, addr, 5);
    writeReg(NRF_REG_RX_PW_P0, NRF_PAYLOAD);
    writeReg(NRF_REG_STATUS, 0x70);            // clear STATUS
    flushTx();
    flushRx();
    writeReg(NRF_REG_CONFIG, 0x0E);            // PWR_UP + 2-byte CRC
    esp_rom_delay_us(2000);
}

void nrf24_init(void) {
    if (!spi_inited) {
        // SPI 总线初始化（仅首次）
        spi_bus_config_t buscfg = {
            .mosi_io_num = NRF_SPI_PIN_MOSI,
            .miso_io_num = NRF_SPI_PIN_MISO,
            .sclk_io_num = NRF_SPI_PIN_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 64,
        };
        ESP_ERROR_CHECK(spi_bus_initialize(NRF_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

        spi_device_interface_config_t devcfg = {
            .clock_speed_hz = NRF_SPI_HZ,
            .mode = 0,
            .spics_io_num = NRF_SPI_PIN_CSN,
            .queue_size = 1,
            .flags = SPI_DEVICE_HALFDUPLEX,
        };
        ESP_ERROR_CHECK(spi_bus_add_device(NRF_SPI_HOST, &devcfg, &spi_dev));

        // CE 引脚
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << NRF_PIN_CE),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        gpio_set_level(NRF_PIN_CE, 0);
        spi_inited = true;
    }

    gpio_set_level(NRF_PIN_CE, 0);
    esp_rom_delay_us(10000);

    nrf24_config_regs();

    txSeq = 0;
    txPending = false;
    s_last_ack_us = 0;
    ESP_LOGI(TAG, "nRF24 initialized: ch=%d payload=%d", NRF_CHANNEL, NRF_PAYLOAD);
}

void nrf24_submitPacket(const uint8_t *data) {
    if (!spi_dev) return;
    writeReg(NRF_REG_STATUS, 0x70);  // clear STATUS

    uint8_t tx[1 + NRF_PAYLOAD];
    tx[0] = NRF_CMD_W_TX_PAYLOAD;
    for (int i = 0; i < NRF_PAYLOAD; i++) tx[1 + i] = data[i];
    spi_transaction_t t = {.length = (1 + NRF_PAYLOAD) * 8, .tx_buffer = tx};
    spi_device_polling_transmit(spi_dev, &t);

    txStartUs = esp_timer_get_time();
    cePulse(NRF_CE_PULSE_US);
    txPending = true;
}

int8_t nrf24_pollTxComplete(void) {
    if (!spi_dev || !txPending) return 1;

    // 软件超时（2.5ms）
    if ((esp_timer_get_time() - txStartUs) >= NRF_TX_TIMEOUT_US) {
        writeReg(NRF_REG_STATUS, 0x70);
        flushTx();
        txPending = false;
        return 0;
    }
    uint8_t st = readReg(NRF_REG_STATUS);
    if (st & NRF_STATUS_TX_DS) {
        writeReg(NRF_REG_STATUS, NRF_STATUS_TX_DS);
        txPending = false;
        s_last_ack_us = esp_timer_get_time();
        return 1;
    }
    if (st & NRF_STATUS_MAX_RT) {
        writeReg(NRF_REG_STATUS, NRF_STATUS_MAX_RT);
        flushTx();
        txPending = false;
        return 0;
    }
    return -1;  // 仍在进行
}

void nrf24_sendFrame(uint8_t inputMode, uint16_t buttons, uint8_t dpad,
                     uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry,
                     uint8_t lt, uint8_t rt) {
    // 15 字节 payload，与 nrf24_link.cpp::submitInputFrame 完全一致
    uint8_t payload[NRF_PAYLOAD];
    payload[0]  = inputMode;
    payload[1]  = txSeq++;
    payload[2]  = (uint8_t)(buttons & 0xFF);
    payload[3]  = (uint8_t)(buttons >> 8);
    payload[4]  = dpad;
    payload[5]  = (uint8_t)(lx & 0xFF);
    payload[6]  = (uint8_t)(lx >> 8);
    payload[7]  = (uint8_t)(ly & 0xFF);
    payload[8]  = (uint8_t)(ly >> 8);
    payload[9]  = (uint8_t)(rx & 0xFF);
    payload[10] = (uint8_t)(rx >> 8);
    payload[11] = (uint8_t)(ry & 0xFF);
    payload[12] = (uint8_t)(ry >> 8);
    payload[13] = lt;
    payload[14] = rt;
    nrf24_submitPacket(payload);
}

void nrf24_reinit(void) {
    nrf24_init();
}

bool nrf24_link_ok(void) {
    if (s_last_ack_us == 0) return false;
    return (esp_timer_get_time() - s_last_ack_us) < NRF_LINK_TIMEOUT_US;
}
