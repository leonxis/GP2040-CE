#include "addons/wireless_receiver.h"
#include "addons/nrf24.h"
#include "NeoPico.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "system.h"
#include "tusb.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#include "enums.pb.h"

NRF24 radio;

// 全局单例（core0: setup/preprocess，core1: process）
WirelessReceiver wirelessReceiver;

#if WIRELESS_RX_RGB_LED_PIN >= 0
static NeoPico rxNeo;
static uint32_t rxNeoFrame[100] = {0};

static void rxNeoSetColor(uint8_t r, uint8_t g, uint8_t b) {
    rxNeoFrame[0] = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    rxNeo.SetFrame(rxNeoFrame);
    rxNeo.Show();
}
#endif

void WirelessReceiver::setup() {
    // SPI1 pins for the radio
    gpio_set_function(WIRELESS_RX_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(WIRELESS_RX_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(WIRELESS_RX_MISO_PIN, GPIO_FUNC_SPI);
    radio.begin(WIRELESS_RX_SPI_INST, WIRELESS_RX_CSN_PIN, WIRELESS_RX_CE_PIN);

    paired = Storage::getInstance().getGamepadOptions().wirelessPaired;
    rxButtons = 0;
    rxDpad = 0;
    rxlx = GAMEPAD_JOYSTICK_MID; rxly = GAMEPAD_JOYSTICK_MID;
    rxrx = GAMEPAD_JOYSTICK_MID; rxry = GAMEPAD_JOYSTICK_MID;
    rxlt = 0; rxrt = 0;
    lastPacketTime = 0;
    lastRadioReinit = 0;
    if (!paired) {
        tud_disconnect(); // no gamepad identity until paired
    }
    radio.startListening();

#if WIRELESS_RX_RGB_LED_PIN >= 0
    // WS2812 状态灯（pio0，与 USB0 PIO-USB 主机(pio1)分离，避免冲突）
    rxNeo.Setup(WIRELESS_RX_RGB_LED_PIN, 1, LED_FORMAT_GRB, pio0, 0);
    rxNeo.Off();
#else
    gpio_init(WIRELESS_RX_LED_PIN);
    gpio_set_dir(WIRELESS_RX_LED_PIN, GPIO_OUT);
    gpio_put(WIRELESS_RX_LED_PIN, 0);
#endif
    lastLedToggle = 0;
    ledOn = false;
    ledState = 0;   // 强制 process() 首轮即按真实链路点灯（避免上电后 5s 无灯）
}

void WirelessReceiver::handlePacket(const uint8_t *pkt) {
    uint8_t mode = pkt[0];
    GamepadOptions &opts = Storage::getInstance().getGamepadOptions();

    if (mode != 0xFF &&
        mode != INPUT_MODE_XINPUT && mode != INPUT_MODE_SWITCH && mode != INPUT_MODE_PS3 &&
        mode != INPUT_MODE_KEYBOARD && mode != INPUT_MODE_PS4 && mode != INPUT_MODE_XBONE &&
        mode != INPUT_MODE_MDMINI && mode != INPUT_MODE_NEOGEO && mode != INPUT_MODE_PCEMINI &&
        mode != INPUT_MODE_EGRET && mode != INPUT_MODE_ASTRO && mode != INPUT_MODE_PSCLASSIC &&
        mode != INPUT_MODE_XBOXORIGINAL && mode != INPUT_MODE_PS5 && mode != INPUT_MODE_GENERIC &&
        mode != INPUT_MODE_SWITCH_PRO && mode != INPUT_MODE_P5GENERAL && mode != INPUT_MODE_SINPUT &&
        mode != INPUT_MODE_SWITCH2_PRO && mode != INPUT_MODE_PS4B && mode != INPUT_MODE_XINPUTB) {
        return;
    }

    // 0xFF = 发送端还没拿到 Pico 的真实输入模式：不配对、不切模式，只缓存输入
    if (mode == 0xFF) {
        rxButtons = pkt[2] | ((uint16_t)pkt[3] << 8);
        rxDpad = pkt[4];
        rxlx = pkt[5] | ((uint16_t)pkt[6] << 8);
        rxly = pkt[7] | ((uint16_t)pkt[8] << 8);
        rxrx = pkt[9] | ((uint16_t)pkt[10] << 8);
        rxry = pkt[11] | ((uint16_t)pkt[12] << 8);
        rxlt = pkt[13];
        rxrt = pkt[14];
        lastPacketTime = to_ms_since_boot(get_absolute_time());
        return;
    }

    if (!paired) {
        opts.wirelessPaired = true;
        paired = true;
        opts.inputMode = (InputMode)mode;
        Storage::getInstance().save(true);
        tud_disconnect();   // 显式断开 D+ 上拉，让主机看到 USB 拔出
        sleep_ms(500);      // 给主机足够时间处理断连 + flash 落盘
        System::reboot(System::BootMode::DEFAULT);
        return;
    }

    if (mode != (uint8_t)opts.inputMode) {
        opts.inputMode = (InputMode)mode;
        Storage::getInstance().save(true);
        tud_disconnect();   // 显式断开 D+ 上拉，让主机看到 USB 拔出
        sleep_ms(500);      // 给主机足够时间处理断连 + flash 落盘
        System::reboot(System::BootMode::DEFAULT);
        return;
    }

    rxButtons = pkt[2] | ((uint16_t)pkt[3] << 8);
    rxDpad = pkt[4];
    rxlx = pkt[5] | ((uint16_t)pkt[6] << 8);
    rxly = pkt[7] | ((uint16_t)pkt[8] << 8);
    rxrx = pkt[9] | ((uint16_t)pkt[10] << 8);
    rxry = pkt[11] | ((uint16_t)pkt[12] << 8);
    rxlt = pkt[13];
    rxrt = pkt[14];
    lastPacketTime = to_ms_since_boot(get_absolute_time());
}

void WirelessReceiver::preprocess() {
    uint8_t pkt[NRF24_PAYLOAD];
    bool got = false;
    while (radio.readPacket(pkt)) {
        got = true;
        handlePacket(pkt);
    }
    // 看门狗：长时间无包则重新初始化射频（3s 防抖）
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!got && now - lastPacketTime > 1000 && now - lastRadioReinit > 3000) {
        lastRadioReinit = now;
        radio.begin(WIRELESS_RX_SPI_INST, WIRELESS_RX_CSN_PIN, WIRELESS_RX_CE_PIN);
        radio.startListening();
    }
    // 重新注入缓存输入；1000ms 无包自动释放
    if (paired && now - lastPacketTime < 1000) {
        Gamepad *g = Storage::getInstance().GetGamepad();
        g->state.buttons = rxButtons;
        g->state.dpad = rxDpad;
        g->state.lx = rxlx;
        g->state.ly = rxly;
        g->state.rx = rxrx;
        g->state.ry = rxry;
        g->state.lt = rxlt;
        g->state.rt = rxrt;
    }
}

void WirelessReceiver::process() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    // paired 是写入 flash 的持久标志（配对一次后永不清除），不能代表实时连接；
    // 绿灯必须由"1s 内确实收到 nRF 包"驱动，否则发射端关机/超距/切 BLE 时会假常亮。
    //   未配对            -> 红色快闪 300ms（等待首次配对）
    //   已配对 + 收包正常  -> 绿色常亮（5s 周期刷新保持）
    //   已配对 + 链路丢失  -> 红色慢闪 1s（发射端关机/超距/切 BLE 停发 nRF）
    bool linkActive = paired && lastPacketTime != 0 && (now - lastPacketTime < 1000);
#if WIRELESS_RX_RGB_LED_PIN >= 0
    // ledState: 0=未知/灭, 1=红(闪烁), 2=绿(常亮)
    uint8_t wantState;
    uint32_t interval;
    if (linkActive) {
        wantState = 2;
        interval  = 5000;                 // 绿色常亮
    } else if (paired) {
        wantState = 1;
        interval  = 1000;                 // 红慢闪：已配对但链路丢失
    } else {
        wantState = 1;
        interval  = 300;                  // 红快闪：从未配对
    }

    bool stateChanged = (wantState != ledState);
    if (stateChanged || now - lastLedToggle >= interval) {
        lastLedToggle = now;
        ledState = wantState;
        if (wantState == 2) {
            ledOn = true;
            rxNeoSetColor(0, 200, 0);     // 绿色常亮
        } else {
            // 状态切入闪烁态时先点亮一次（从绿灯切入立即变红），其余按周期翻转
            ledOn = stateChanged ? true : !ledOn;
            if (ledOn) {
                rxNeoSetColor(200, 0, 0);
            } else {
                rxNeo.Off();
            }
        }
    }
#else
    uint32_t interval = linkActive ? 5000 : (paired ? 1000 : 300);
    if (now - lastLedToggle >= interval) {
        lastLedToggle = now;
        if (linkActive) {
            gpio_put(WIRELESS_RX_LED_PIN, 1);
        } else {
            ledOn = !ledOn;
            gpio_put(WIRELESS_RX_LED_PIN, ledOn ? 1 : 0);
        }
    }
#endif
}
