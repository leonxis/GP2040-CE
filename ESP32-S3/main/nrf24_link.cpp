#include "nrf24_link.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "board_config.h"

// ---------------------------------------------------------------------------
// nRF24L01+ registers / commands (same set as the RP2354 nrf24_link addon)
// ---------------------------------------------------------------------------
#define NRF_REG_CONFIG       0x00
#define NRF_REG_EN_AA        0x01
#define NRF_REG_EN_RXADDR    0x02
#define NRF_REG_SETUP_AW     0x03
#define NRF_REG_SETUP_RETR   0x04
#define NRF_REG_RF_CH        0x05
#define NRF_REG_RF_SETUP     0x06
#define NRF_REG_STATUS       0x07
#define NRF_REG_RX_ADDR_P0   0x0A
#define NRF_REG_TX_ADDR      0x10
#define NRF_REG_RX_PW_P0     0x11

#define NRF_CMD_W_REGISTER   0x20
#define NRF_CMD_W_TX_PAYLOAD 0xA0
#define NRF_CMD_FLUSH_TX     0xE1
#define NRF_CMD_FLUSH_RX     0xE2

#define NRF_STATUS_TX_DS     0x20
#define NRF_STATUS_MAX_RT    0x10

static spi_device_handle_t s_dev;
// start/sleep run on the controller task (Core0); send/poll run on the UART
// RX task (Core1). Flags are volatile single-byte/word; SPI transactions
// themselves are serialized by the IDF device lock.
static volatile bool s_started = false;
static uint8_t s_txSeq = 0;
static volatile bool s_txPending = false;
static int64_t s_txStartUs = 0;
// ACK timestamp in ms (uint32: atomic on Xtensa, wraps safely with
// subtraction compare). Written on Core1, read on Core0 for LINK_STATUS.
static volatile uint32_t s_lastAckMs = 0;
static uint32_t s_failStreak = 0;

static void nrf_xfer(const uint8_t *tx, uint8_t *rx, size_t len) {
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    spi_device_polling_transmit(s_dev, &t);
}

static void nrf_write_reg(uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { (uint8_t)(NRF_CMD_W_REGISTER | reg), val };
    nrf_xfer(tx, nullptr, 2);
}

static uint8_t nrf_read_reg(uint8_t reg) {
    uint8_t tx[2] = { reg, 0 };
    uint8_t rx[2] = { 0 };
    nrf_xfer(tx, rx, 2);
    return rx[1];
}

static void nrf_flush(uint8_t cmd) {
    nrf_xfer(&cmd, nullptr, 1);
}

static void nrf_ce_pulse(uint32_t us) {
    gpio_set_level(NRF24_CE_GPIO, 1);
    esp_rom_delay_us(us);
    gpio_set_level(NRF24_CE_GPIO, 0);
}

// Register sequence identical to nrf24_link.cpp::setup() on wireless-tx-2354.
static void nrf_config_regs(void) {
    nrf_write_reg(NRF_REG_CONFIG, 0x00);        // power down
    nrf_write_reg(NRF_REG_EN_AA, 0x01);         // pipe0 auto-ack
    nrf_write_reg(NRF_REG_EN_RXADDR, 0x01);     // pipe0 only
    nrf_write_reg(NRF_REG_SETUP_AW, 0x03);      // 5-byte addresses
    nrf_write_reg(NRF_REG_SETUP_RETR, 0x03);    // ARD=250us, ARC=3
    nrf_write_reg(NRF_REG_RF_CH, NRF24_CHANNEL);
    nrf_write_reg(NRF_REG_RF_SETUP, 0x0E);      // 2 Mbps, 0 dBm
    const uint8_t addr[5] = NRF24_ADDR_BYTES;
    uint8_t buf[6];
    buf[0] = NRF_CMD_W_REGISTER | NRF_REG_TX_ADDR;
    memcpy(&buf[1], addr, 5);
    nrf_xfer(buf, nullptr, 6);
    buf[0] = NRF_CMD_W_REGISTER | NRF_REG_RX_ADDR_P0;   // ACK returns on P0
    nrf_xfer(buf, nullptr, 6);
    nrf_write_reg(NRF_REG_RX_PW_P0, NRF24_PAYLOAD_LEN);
    nrf_write_reg(NRF_REG_STATUS, 0x70);        // clear IRQ flags
    // FIFOs survive MCU resets while the module stays powered: stale packets
    // would become the first frame after boot. Flush both, like the receiver.
    nrf_flush(NRF_CMD_FLUSH_TX);
    nrf_flush(NRF_CMD_FLUSH_RX);
    nrf_write_reg(NRF_REG_CONFIG, 0x0E);        // PWR_UP + EN_CRC + CRCO(2B)
    esp_rom_delay_us(2000);                     // PowerUp + osc settling
}

void nrf24_link_init(void) {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = NRF24_MOSI_GPIO;
    bus.miso_io_num = NRF24_MISO_GPIO;
    bus.sclk_io_num = NRF24_SCK_GPIO;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED));

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = NRF24_SPI_CLOCK_HZ;
    dev.mode = 0;
    dev.spics_io_num = NRF24_CSN_GPIO;
    dev.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_dev));

    gpio_config_t ce = {};
    ce.pin_bit_mask = 1ULL << NRF24_CE_GPIO;
    ce.mode = GPIO_MODE_OUTPUT;
    gpio_config(&ce);
    gpio_set_level(NRF24_CE_GPIO, 0);

    s_started = false;
}

void nrf24_link_start(void) {
    if (s_started) return;
    gpio_set_level(NRF24_CE_GPIO, 0);
    esp_rom_delay_us(10000);                    // module power settle
    nrf_config_regs();
    s_txSeq = 0;
    s_txPending = false;
    s_lastAckMs = 0;
    s_failStreak = 0;
    s_started = true;
}

void nrf24_link_sleep(void) {
    if (!s_started) return;
    gpio_set_level(NRF24_CE_GPIO, 0);
    nrf_write_reg(NRF_REG_CONFIG, 0x00);        // power down
    s_txPending = false;
    s_started = false;
}

static void nrf_submit_packet(const uint8_t *data) {
    nrf_write_reg(NRF_REG_STATUS, 0x70);        // clear IRQ flags
    uint8_t tx[1 + NRF24_PAYLOAD_LEN];
    tx[0] = NRF_CMD_W_TX_PAYLOAD;
    memcpy(&tx[1], data, NRF24_PAYLOAD_LEN);
    nrf_xfer(tx, nullptr, sizeof(tx));
    s_txStartUs = esp_timer_get_time();
    nrf_ce_pulse(NRF24_CE_PULSE_US);            // trigger transmission
    s_txPending = true;
}

void nrf24_link_send_frame(uint8_t inputMode, uint16_t buttons, uint8_t dpad,
                           uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry,
                           uint8_t lt, uint8_t rt) {
    if (!s_started || s_txPending) return;      // constant stream: skip, no queue

    // 15-byte payload, identical layout to nrf24_link.cpp::submitInputFrame.
    uint8_t payload[NRF24_PAYLOAD_LEN];
    payload[0]  = inputMode;
    payload[1]  = s_txSeq++;
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
    nrf_submit_packet(payload);
}

void nrf24_link_poll(void) {
    if (!s_started || !s_txPending) return;

    // Software timeout first (pure time compare, no SPI traffic):
    // SETUP_RETR 0x03 bounds a hardware transaction to ~1.14 ms;
    // 2.5 ms without TX_DS/MAX_RT means the chip misbehaved — recover.
    if ((esp_timer_get_time() - s_txStartUs) >= NRF24_TX_TIMEOUT_US) {
        nrf_write_reg(NRF_REG_STATUS, 0x70);
        nrf_flush(NRF_CMD_FLUSH_TX);
        s_txPending = false;
        if (++s_failStreak >= NRF24_FAIL_REINIT_THRESHOLD) {
            s_started = false;
            nrf24_link_start();                 // watchdog reinit
        }
        return;
    }

    const uint8_t st = nrf_read_reg(NRF_REG_STATUS);
    if (st & NRF_STATUS_TX_DS) {
        nrf_write_reg(NRF_REG_STATUS, NRF_STATUS_TX_DS);
        s_txPending = false;
        s_lastAckMs = (uint32_t)(esp_timer_get_time() / 1000);
        s_failStreak = 0;
    } else if (st & NRF_STATUS_MAX_RT) {
        nrf_write_reg(NRF_REG_STATUS, NRF_STATUS_MAX_RT);
        nrf_flush(NRF_CMD_FLUSH_TX);
        s_txPending = false;
        if (++s_failStreak >= NRF24_FAIL_REINIT_THRESHOLD) {
            s_started = false;
            nrf24_link_start();
        }
    }
    // else: still in flight, judge on a later call
}

bool nrf24_link_ok(void) {
    if (s_lastAckMs == 0) return false;
    const uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
    return (nowMs - s_lastAckMs) < (NRF24_LINK_DOWN_TIMEOUT_US / 1000);
}
