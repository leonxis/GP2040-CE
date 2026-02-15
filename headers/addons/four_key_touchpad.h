#ifndef _FOUR_KEY_TOUCHPAD_H
#define _FOUR_KEY_TOUCHPAD_H

#include "gpaddon.h"
#include "BoardConfig.h"

#define FOUR_KEY_TOUCHPAD_ADDON_NAME "4键触摸板"

// BS814A-2: DATA = I2C1 SCL (MCU reads), SCK = I2C1 SDA (MCU drives). Bit-bang 4 bits; low = pressed; map to A1–A4.

class FourKeyTouchpadAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void process();
    virtual void preprocess();
    virtual void postprocess(bool) {}
    virtual std::string name() { return FOUR_KEY_TOUCHPAD_ADDON_NAME; }
    virtual void reinit() {}
private:
    int32_t pin_sck = -1;   // I2C1 SDA: clock output
    int32_t pin_data = -1;  // I2C1 SCL: data input (release=high, press=low)
    bool anyTouchKeyPressed = false;  // 本帧在 GPIO12 按下时是否有触摸键按下（供 process 中屏蔽 GPIO12 映射）
    uint8_t lastKeyNibble = 0x0F;     // 上次有效键状态（1=松键），节流/错误冷却时复用
    uint32_t lastPollTime = 0;        // 上次轮询时间 (time_us_32)
    uint32_t nextReadAllowed = 0;     // 读错后 6ms 内不再读 (time_us_32)
    uint8_t partialByte = 0;          // 分帧累积的 8 位
    uint8_t readPhase = 0;            // 0..3：本周期已读 2*readPhase 位，再读 2 位
};

#endif
