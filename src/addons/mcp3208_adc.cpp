#include "addons/mcp3208_adc.h"
#include "config.pb.h"
#include "enums.pb.h"
#include "storagemanager.h"
#include "peripheralmanager.h"
#include "drivermanager.h"
#include "gamepad/GamepadState.h"
#include "gamepad.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ========== CH2/CH5 四档开关防抖（编译时修改） ==========
// 连续 N 帧同档位才更新输出，避免电压过渡误触发。主循环约 1ms/帧，N 帧 ≈ N ms 延迟。
static const uint8_t CH25_DEBOUNCE_FRAMES = 4;   // 防抖帧数，建议 1–4，按需改

// ========== 一轮执行时间估算（每帧 preprocess + process）==========
// preprocess: readAllChannels 仅做 SPI 读取
//   - 6 通道 × 每通道 3 字节 = 18 字节 = 144 bit @ 1.5MHz → 144/1.5 ≈ 96 µs 纯 SPI 时钟
//   - 6 次 select/deselect（GPIO）、1 次 setBaudrate，约 10–25 µs
//   - 合计 readAllChannels ≈ 105–125 µs
// process: 摇杆 + CH2/CH5
//   - Storage/Driver 访问、曲线预设检测：约 5–15 µs
//   - 双摇杆：getStickRaw、atan2、getInterpolatedScale、死区/反死区、曲线段查表、写 state：约 25–60 µs（无曲线偏下，有曲线偏上）
//   - applyCh2Ch5Keys：阈值查表 + 防抖 + mask 应用：约 2–5 µs
//   - 合计 process ≈ 35–85 µs
// 整轮（preprocess + process）≈ 140–210 µs，典型约 170 µs（1.5MHz SPI、双摇杆、无曲线或轻量曲线）
// CH2/CH5 四档开关：按电压范围划分（两档中点），抗波动。Vref=3.3V，12bit raw = V/3.3*4095
// 左MT: 0～0.4V, L3: 0.4～1.2V, Ext左扳机: 1.2～2.0V, 左FN: 2.0～2.9V（CH5 同理）
static const uint16_t CH25_T1 = 496;   // 0.4V
static const uint16_t CH25_T2 = 1488;  // 1.2V
static const uint16_t CH25_T3 = 2482;  // 2.0V
static const uint16_t CH25_T4 = 3596;  // 2.9V，raw >= T4 视为无效(-1)

// 从 GpioMappingInfo 得到要写入 gamepad state 的 (buttons, dpad) mask
static void gpioMappingToMasks(const GpioMappingInfo& m, uint32_t* out_buttons, uint32_t* out_dpad) {
    *out_buttons = 0;
    *out_dpad = 0;
    if (m.action == GpioAction::NONE) return;
    if (m.action == GpioAction::CUSTOM_BUTTON_COMBO) {
        *out_buttons = m.customButtonMask;
        if (m.customDpadMask & GAMEPAD_MASK_DU) *out_dpad |= GAMEPAD_MASK_UP;
        if (m.customDpadMask & GAMEPAD_MASK_DD) *out_dpad |= GAMEPAD_MASK_DOWN;
        if (m.customDpadMask & GAMEPAD_MASK_DL) *out_dpad |= GAMEPAD_MASK_LEFT;
        if (m.customDpadMask & GAMEPAD_MASK_DR) *out_dpad |= GAMEPAD_MASK_RIGHT;
        return;
    }
    switch (m.action) {
        case GpioAction::BUTTON_PRESS_UP:    *out_dpad |= GAMEPAD_MASK_UP; break;
        case GpioAction::BUTTON_PRESS_DOWN:  *out_dpad |= GAMEPAD_MASK_DOWN; break;
        case GpioAction::BUTTON_PRESS_LEFT:  *out_dpad |= GAMEPAD_MASK_LEFT; break;
        case GpioAction::BUTTON_PRESS_RIGHT: *out_dpad |= GAMEPAD_MASK_RIGHT; break;
        case GpioAction::BUTTON_PRESS_B1:   *out_buttons |= GAMEPAD_MASK_B1; break;
        case GpioAction::BUTTON_PRESS_B2:   *out_buttons |= GAMEPAD_MASK_B2; break;
        case GpioAction::BUTTON_PRESS_B3:   *out_buttons |= GAMEPAD_MASK_B3; break;
        case GpioAction::BUTTON_PRESS_B4:   *out_buttons |= GAMEPAD_MASK_B4; break;
        case GpioAction::BUTTON_PRESS_L1:   *out_buttons |= GAMEPAD_MASK_L1; break;
        case GpioAction::BUTTON_PRESS_R1:   *out_buttons |= GAMEPAD_MASK_R1; break;
        case GpioAction::BUTTON_PRESS_L2:   *out_buttons |= GAMEPAD_MASK_L2; break;
        case GpioAction::BUTTON_PRESS_R2:   *out_buttons |= GAMEPAD_MASK_R2; break;
        case GpioAction::BUTTON_PRESS_S1:   *out_buttons |= GAMEPAD_MASK_S1; break;
        case GpioAction::BUTTON_PRESS_S2:   *out_buttons |= GAMEPAD_MASK_S2; break;
        case GpioAction::BUTTON_PRESS_L3:   *out_buttons |= GAMEPAD_MASK_L3; break;
        case GpioAction::BUTTON_PRESS_R3:   *out_buttons |= GAMEPAD_MASK_R3; break;
        case GpioAction::BUTTON_PRESS_A1:   *out_buttons |= GAMEPAD_MASK_A1; break;
        case GpioAction::BUTTON_PRESS_A2:   *out_buttons |= GAMEPAD_MASK_A2; break;
        case GpioAction::BUTTON_PRESS_A3:   *out_buttons |= GAMEPAD_MASK_A3; break;
        case GpioAction::BUTTON_PRESS_A4:   *out_buttons |= GAMEPAD_MASK_A4; break;
        case GpioAction::BUTTON_PRESS_E1:   *out_buttons |= GAMEPAD_MASK_E1; break;
        case GpioAction::BUTTON_PRESS_E2:   *out_buttons |= GAMEPAD_MASK_E2; break;
        case GpioAction::BUTTON_PRESS_E3:   *out_buttons |= GAMEPAD_MASK_E3; break;
        case GpioAction::BUTTON_PRESS_E4:   *out_buttons |= GAMEPAD_MASK_E4; break;
        case GpioAction::BUTTON_PRESS_E5:   *out_buttons |= GAMEPAD_MASK_E5; break;
        case GpioAction::BUTTON_PRESS_E6:   *out_buttons |= GAMEPAD_MASK_E6; break;
        case GpioAction::BUTTON_PRESS_E7:   *out_buttons |= GAMEPAD_MASK_E7; break;
        case GpioAction::BUTTON_PRESS_E8:   *out_buttons |= GAMEPAD_MASK_E8; break;
        case GpioAction::BUTTON_PRESS_E9:   *out_buttons |= GAMEPAD_MASK_E9; break;
        case GpioAction::BUTTON_PRESS_E10:  *out_buttons |= GAMEPAD_MASK_E10; break;
        case GpioAction::BUTTON_PRESS_E11:  *out_buttons |= GAMEPAD_MASK_E11; break;
        case GpioAction::BUTTON_PRESS_E12:  *out_buttons |= GAMEPAD_MASK_E12; break;
        default: break;
    }
}

static void convertCurvePoints(const CurvePoint* pb_pts, int count, MCP3208CurvePoint* out) {
    for (int i = 0; i < count && i < 3; i++) {
        out[i].x = pb_pts[i].x;
        out[i].y = pb_pts[i].y;
        out[i].buttonMask = pb_pts[i].buttonMask;
    }
}

bool MCP3208ADCAddon::available() {
    const MCP3208Options& opts = Storage::getInstance().getAddonOptions().mcp3208Options;
    if (!opts.enabled)
        return false;
    uint8_t block = opts.has_spiBlock ? (uint8_t)opts.spiBlock : 0;
    return PeripheralManager::getInstance().isSPIEnabled(block);
}

// Static instance for webconfig to read raw stick values (no AddonManager dependency)
MCP3208ADCAddon* MCP3208ADCAddon::s_instance = nullptr;

bool MCP3208ADCAddon::getRawStickForWebConfig(uint8_t stickNum, uint16_t& x, uint16_t& y) {
    if (s_instance == nullptr || !s_instance->spiOk_ || stickNum >= MCP3208_STICK_COUNT) {
        return false;
    }
    // Refresh ADC values on demand so web calibration gets current data even if main loop is busy
    s_instance->readAllChannels();
    x = (stickNum == 0) ? s_instance->adcValues_[0] : s_instance->adcValues_[7];
    y = (stickNum == 0) ? s_instance->adcValues_[1] : s_instance->adcValues_[6];
    return true;
}

void MCP3208ADCAddon::setup() {
    s_instance = this;
    spiOk_ = false;
    csPin_ = -1;
    // Initialize stick channels to center so first frame is (0,0) not (-1,1) before readAllChannels
    const uint16_t center = static_cast<uint16_t>(MCP3208_ADC_MAX_HALF);
    for (int i = 0; i < 8; i++) adcValues_[i] = 0;
    adcValues_[0] = adcValues_[1] = adcValues_[6] = adcValues_[7] = center;
    ch2_stable_level_ = ch2_pending_level_ = ch5_stable_level_ = ch5_pending_level_ = -1;
    ch2_debounce_count_ = ch5_debounce_count_ = 0;
    last_ch2_buttons_ = last_ch2_dpad_ = last_ch5_buttons_ = last_ch5_dpad_ = 0;
    last_ch2_keyboard_ = last_ch5_keyboard_ = 0;

    const MCP3208Options& opts = Storage::getInstance().getAddonOptions().mcp3208Options;
    uint8_t block = opts.has_spiBlock ? (uint8_t)opts.spiBlock : 0;
    if (!opts.has_csPin) return;
    csPin_ = (int8_t)opts.csPin;
    PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(block);
    if (!spi || !spi->configured) return;
    spi_ = spi;
    spi_->beginTransaction(MCP3208_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);

    const AnalogOptions& o = Storage::getInstance().getAddonOptions().analogOptions;
    usage_curve_profile_1_ = o.curve_profile_1;
    usage_curve_profile_2_ = o.curve_profile_2;
    temp_curve_storage_[1].is_saved = false;
    temp_curve_storage_[1].saved_points_count = 0;
    active_activation_preset_[1] = 0;

    bool curveEnabled = o.joystick_curve_enabled;

    // Stick 0: 左摇杆 CH0=X, CH1=Y
    adc_pairs_[0].analog_invert = o.analogAdc1Invert;
    adc_pairs_[0].analog_dpad = DpadMode::DPAD_MODE_LEFT_ANALOG;
    adc_pairs_[0].in_deadzone = o.inner_deadzone / 100.0f;
    adc_pairs_[0].anti_deadzone = std::clamp(o.anti_deadzone / 100.0f, 0.0f, 1.0f);
    adc_pairs_[0].fixed_anti_deadzone = o.fixed_anti_deadzone;
    adc_pairs_[0].jitter_filter = o.joystick_jitter_filter_1;
    adc_pairs_[0].last_x_adc = 0;
    adc_pairs_[0].last_y_adc = 0;
    adc_pairs_[0].has_range_calibration = (o.joystick_range_data_1_count > 0);
    for (int i = 0; i < MCP3208_CIRCULARITY_SIZE; i++) {
        if (i < o.joystick_range_data_1_count && o.joystick_range_data_1[i] > 0.0f) {
            adc_pairs_[0].range_data[i] = o.joystick_range_data_1[i];
        } else {
            adc_pairs_[0].range_data[i] = 0.0f;
        }
    }
    adc_pairs_[0].finetune_shape_force_circular = o.joystick_finetune_shape_force_circular_1;
    adc_pairs_[0].finetune_shape_amplify = o.joystick_finetune_shape_amplify_1;
    adc_pairs_[0].curve_points_sorted_count = 0;
    adc_pairs_[0].curve_segments_count = 0;
    adc_pairs_[0].active_control_points_mask = 0;
    adc_pairs_[0].x_center = (o.joystick_center_x > 0) ? (uint16_t)o.joystick_center_x : (uint16_t)MCP3208_ADC_MAX_HALF;
    adc_pairs_[0].y_center = (o.joystick_center_y > 0) ? (uint16_t)o.joystick_center_y : (uint16_t)MCP3208_ADC_MAX_HALF;
    adc_pairs_[0].x_value = adc_pairs_[0].y_value = MCP3208_ANALOG_CENTER;
    if (curveEnabled && o.joystick_curve_points_1_count > 0) {
        MCP3208CurvePoint conv[3];
        convertCurvePoints(o.joystick_curve_points_1, o.joystick_curve_points_1_count, conv);
        initializeCurveSegments(0, conv, o.joystick_curve_points_1_count);
    }

    // Stick 1: 右摇杆 CH7=X, CH6=Y
    adc_pairs_[1].analog_invert = o.analogAdc2Invert;
    adc_pairs_[1].analog_dpad = DpadMode::DPAD_MODE_RIGHT_ANALOG;
    adc_pairs_[1].in_deadzone = o.inner_deadzone2 / 100.0f;
    adc_pairs_[1].anti_deadzone = std::clamp(o.anti_deadzone2 / 100.0f, 0.0f, 1.0f);
    adc_pairs_[1].fixed_anti_deadzone = o.fixed_anti_deadzone2;
    adc_pairs_[1].jitter_filter = o.joystick_jitter_filter_2;
    adc_pairs_[1].last_x_adc = 0;
    adc_pairs_[1].last_y_adc = 0;
    adc_pairs_[1].has_range_calibration = (o.joystick_range_data_2_count > 0);
    for (int i = 0; i < MCP3208_CIRCULARITY_SIZE; i++) {
        if (i < o.joystick_range_data_2_count && o.joystick_range_data_2[i] > 0.0f) {
            adc_pairs_[1].range_data[i] = o.joystick_range_data_2[i];
        } else {
            adc_pairs_[1].range_data[i] = 0.0f;
        }
    }
    adc_pairs_[1].finetune_shape_force_circular = o.joystick_finetune_shape_force_circular_2;
    adc_pairs_[1].finetune_shape_amplify = o.joystick_finetune_shape_amplify_2;
    adc_pairs_[1].curve_points_sorted_count = 0;
    adc_pairs_[1].curve_segments_count = 0;
    adc_pairs_[1].active_control_points_mask = 0;
    adc_pairs_[1].x_center = (o.joystick_center_x2 > 0) ? (uint16_t)o.joystick_center_x2 : (uint16_t)MCP3208_ADC_MAX_HALF;
    adc_pairs_[1].y_center = (o.joystick_center_y2 > 0) ? (uint16_t)o.joystick_center_y2 : (uint16_t)MCP3208_ADC_MAX_HALF;
    adc_pairs_[1].x_value = adc_pairs_[1].y_value = MCP3208_ANALOG_CENTER;
    if (curveEnabled && o.joystick_curve_points_2_count > 0) {
        MCP3208CurvePoint conv[3];
        convertCurvePoints(o.joystick_curve_points_2, o.joystick_curve_points_2_count, conv);
        initializeCurveSegments(1, conv, o.joystick_curve_points_2_count);
    }

    applyFinetuneShapeAdjustments(0);
    applyFinetuneShapeAdjustments(1);
    buildCh25Maps();
    spiOk_ = true;
}

void MCP3208ADCAddon::readAllChannels()
{
    if (!spi_ || !spiOk_) return;

    static const uint8_t channels[MCP3208_READ_CHANNELS] = {0, 1, 2, 5, 6, 7};

    spi_->setBaudrate(MCP3208_SPI_HZ);

    for (int i = 0; i < MCP3208_READ_CHANNELS; i++) {
        spi_->select(csPin_);

        uint8_t ch = channels[i];
        uint8_t tx[3] = {
            static_cast<uint8_t>(0x06 | ((ch >> 2) & 0x01)),
            static_cast<uint8_t>((ch & 0x03) << 6),
            0x00
        };
        uint8_t rx[3];

        spi_->transfer(tx, rx, 3);

        spi_->deselect();

        adcValues_[ch] = ((rx[1] & 0x0F) << 8) | rx[2];
    }
}

float MCP3208ADCAddon::getStickRaw(int stick, bool isX) {
    uint16_t adc_value = (stick == 0) ? (isX ? adcValues_[0] : adcValues_[1]) : (isX ? adcValues_[7] : adcValues_[6]);
    uint32_t threshold = adc_pairs_[stick].jitter_filter;
    uint16_t* last_adc = isX ? &adc_pairs_[stick].last_x_adc : &adc_pairs_[stick].last_y_adc;
    if (threshold > 0) {
        uint32_t diff = (adc_value > *last_adc) ? (adc_value - *last_adc) : (*last_adc - adc_value);
        if (diff < threshold) return static_cast<float>(*last_adc);
        *last_adc = adc_value;
    } else {
        *last_adc = adc_value;
    }
    return static_cast<float>(adc_value);
}

void MCP3208ADCAddon::preprocess() {
    if (!spiOk_) return;
    readAllChannels();
}

float MCP3208ADCAddon::getInterpolatedScale(int stick, float angle) const {
    if (!adc_pairs_[stick].has_range_calibration) return 0.65f;
    float norm = (angle + (float)M_PI) / (2.0f * (float)M_PI);
    float idx = norm * MCP3208_CIRCULARITY_SIZE;
    int i0 = (int)idx;
    if (i0 >= MCP3208_CIRCULARITY_SIZE) i0 = MCP3208_CIRCULARITY_SIZE - 1;
    int i1 = (i0 + 1 < MCP3208_CIRCULARITY_SIZE) ? (i0 + 1) : 0;
    float t = idx - (float)i0;
    float r0 = adc_pairs_[stick].range_data[i0];
    float r1 = adc_pairs_[stick].range_data[i1];
    return r0 * (1.0f - t) + r1 * t;
}

void MCP3208ADCAddon::applyFinetuneShapeAdjustments(int stick) {
    if (!adc_pairs_[stick].has_range_calibration) return;
    if (!adc_pairs_[stick].finetune_shape_force_circular) {
        float mn = adc_pairs_[stick].range_data[0];
        for (int i = 1; i < MCP3208_CIRCULARITY_SIZE; i++)
            if (adc_pairs_[stick].range_data[i] < mn) mn = adc_pairs_[stick].range_data[i];
        for (int i = 0; i < MCP3208_CIRCULARITY_SIZE; i++)
            adc_pairs_[stick].range_data[i] = mn;
    }
    float amp = 1.0f + adc_pairs_[stick].finetune_shape_amplify / 100.0f;
    if (amp > 0.0f) {
        for (int i = 0; i < MCP3208_CIRCULARITY_SIZE; i++)
            if (adc_pairs_[stick].range_data[i] > 0.0f)
                adc_pairs_[stick].range_data[i] /= amp;
    }
}

void MCP3208ADCAddon::initializeCurveSegments(int stick, const MCP3208CurvePoint* control_points, int control_points_count) {
    adc_pairs_[stick].curve_points_sorted_count = 0;
    adc_pairs_[stick].curve_points_sorted[adc_pairs_[stick].curve_points_sorted_count++] = {
        adc_pairs_[stick].in_deadzone, adc_pairs_[stick].anti_deadzone, 0u
    };
    for (int i = 0; i < control_points_count; i++)
        adc_pairs_[stick].curve_points_sorted[adc_pairs_[stick].curve_points_sorted_count++] = {
            control_points[i].x, control_points[i].y, control_points[i].buttonMask
        };
    adc_pairs_[stick].curve_points_sorted[adc_pairs_[stick].curve_points_sorted_count++] = { 1.0f, 1.0f, 0u };
    adc_pairs_[stick].curve_segments_count = 0;
    for (int i = 0; i < adc_pairs_[stick].curve_points_sorted_count - 1; i++) {
        float p1x = adc_pairs_[stick].curve_points_sorted[i].x, p1y = adc_pairs_[stick].curve_points_sorted[i].y;
        float p2x = adc_pairs_[stick].curve_points_sorted[i+1].x, p2y = adc_pairs_[stick].curve_points_sorted[i+1].y;
        if (p2x == p1x) {
            adc_pairs_[stick].curve_segments[adc_pairs_[stick].curve_segments_count].slope = 0.0f;
            adc_pairs_[stick].curve_segments[adc_pairs_[stick].curve_segments_count].intercept = p1y;
        } else {
            float sl = (p2y - p1y) / (p2x - p1x);
            adc_pairs_[stick].curve_segments[adc_pairs_[stick].curve_segments_count].slope = sl;
            adc_pairs_[stick].curve_segments[adc_pairs_[stick].curve_segments_count].intercept = p1y - p1x * sl;
        }
        adc_pairs_[stick].curve_segments[adc_pairs_[stick].curve_segments_count].x_start = p1x;
        adc_pairs_[stick].curve_segments[adc_pairs_[stick].curve_segments_count].x_end = p2x;
        adc_pairs_[stick].curve_segments_count++;
    }
}

void MCP3208ADCAddon::forceReleaseActiveControlPoints(int stick, Gamepad* gamepad) {
    if (!gamepad || adc_pairs_[stick].active_control_points_mask == 0) {
        adc_pairs_[stick].active_control_points_mask = 0;
        return;
    }
    uint32_t to_release = 0;
    for (int j = 1; j < adc_pairs_[stick].curve_points_sorted_count - 1; j++) {
        if ((adc_pairs_[stick].active_control_points_mask & (1U << (j-1))) != 0)
            to_release |= adc_pairs_[stick].curve_points_sorted[j].buttonMask;
    }
    uint32_t dpad_mask = to_release & (GAMEPAD_MASK_DU | GAMEPAD_MASK_DD | GAMEPAD_MASK_DL | GAMEPAD_MASK_DR);
    if (dpad_mask & GAMEPAD_MASK_DU) gamepad->state.dpad &= ~GAMEPAD_MASK_UP;
    if (dpad_mask & GAMEPAD_MASK_DD) gamepad->state.dpad &= ~GAMEPAD_MASK_DOWN;
    if (dpad_mask & GAMEPAD_MASK_DL) gamepad->state.dpad &= ~GAMEPAD_MASK_LEFT;
    if (dpad_mask & GAMEPAD_MASK_DR) gamepad->state.dpad &= ~GAMEPAD_MASK_RIGHT;
    uint32_t reg = to_release & ~(GAMEPAD_MASK_DU|GAMEPAD_MASK_DD|GAMEPAD_MASK_DL|GAMEPAD_MASK_DR);
    if (reg) gamepad->state.buttons &= ~reg;
    adc_pairs_[stick].active_control_points_mask = 0;
}

void MCP3208ADCAddon::applyResponseCurveToCoordinates(float& nx, float& ny, int stick, Gamepad* gamepad) {
    if (nx == 0.0f && ny == 0.0f) {
        if (active_activation_preset_[stick] == 0)
            forceReleaseActiveControlPoints(stick, gamepad);
        return;
    }
    float clampdist_sq = nx*nx + ny*ny;
    float clampdist = std::sqrt(clampdist_sq);
    int segmentIdx = -1;
    if (clampdist > 1.0f) segmentIdx = adc_pairs_[stick].curve_segments_count;
    else {
        for (int i = 0; i < adc_pairs_[stick].curve_segments_count; i++) {
            if (clampdist >= adc_pairs_[stick].curve_segments[i].x_start &&
                clampdist < adc_pairs_[stick].curve_segments[i].x_end) {
                segmentIdx = i; break;
            }
        }
        if (segmentIdx == -1) segmentIdx = adc_pairs_[stick].curve_segments_count - 1;
    }
    if (gamepad && active_activation_preset_[stick] == 0) {
        uint8_t old_mask = adc_pairs_[stick].active_control_points_mask;
        uint8_t new_mask = 0;
        uint32_t btn_now = 0, btn_prev = 0;
        for (int j = 1; j < adc_pairs_[stick].curve_points_sorted_count - 1; j++) {
            uint32_t btn = adc_pairs_[stick].curve_points_sorted[j].buttonMask;
            uint8_t bit = 1U << (j-1);
            if (segmentIdx >= j) { new_mask |= bit; if (btn) btn_now |= btn; }
            if ((old_mask & bit) && btn) btn_prev |= btn;
        }
        uint32_t dpad_now = btn_now & (GAMEPAD_MASK_DU|GAMEPAD_MASK_DD|GAMEPAD_MASK_DL|GAMEPAD_MASK_DR);
        uint32_t dpad_prev = btn_prev & (GAMEPAD_MASK_DU|GAMEPAD_MASK_DD|GAMEPAD_MASK_DL|GAMEPAD_MASK_DR);
        uint32_t reg_now = btn_now & ~(GAMEPAD_MASK_DU|GAMEPAD_MASK_DD|GAMEPAD_MASK_DL|GAMEPAD_MASK_DR);
        uint32_t reg_prev = btn_prev & ~(GAMEPAD_MASK_DU|GAMEPAD_MASK_DD|GAMEPAD_MASK_DL|GAMEPAD_MASK_DR);
        if (dpad_now & GAMEPAD_MASK_DU) gamepad->state.dpad |= GAMEPAD_MASK_UP;
        if (dpad_now & GAMEPAD_MASK_DD) gamepad->state.dpad |= GAMEPAD_MASK_DOWN;
        if (dpad_now & GAMEPAD_MASK_DL) gamepad->state.dpad |= GAMEPAD_MASK_LEFT;
        if (dpad_now & GAMEPAD_MASK_DR) gamepad->state.dpad |= GAMEPAD_MASK_RIGHT;
        uint32_t dpad_rel = dpad_prev & ~dpad_now;
        if (dpad_rel & GAMEPAD_MASK_DU) gamepad->state.dpad &= ~GAMEPAD_MASK_UP;
        if (dpad_rel & GAMEPAD_MASK_DD) gamepad->state.dpad &= ~GAMEPAD_MASK_DOWN;
        if (dpad_rel & GAMEPAD_MASK_DL) gamepad->state.dpad &= ~GAMEPAD_MASK_LEFT;
        if (dpad_rel & GAMEPAD_MASK_DR) gamepad->state.dpad &= ~GAMEPAD_MASK_RIGHT;
        gamepad->state.buttons |= reg_now;
        gamepad->state.buttons &= ~(reg_prev & ~reg_now);
        adc_pairs_[stick].active_control_points_mask = new_mask;
    }
    if (clampdist > 1.0f) return;
    int vi = std::min(segmentIdx, (int)adc_pairs_[stick].curve_segments_count - 1);
    float curvedMag = (vi >= 0) ? adc_pairs_[stick].curve_segments[vi].intercept + clampdist * adc_pairs_[stick].curve_segments[vi].slope : clampdist;
    float scale = curvedMag / clampdist;
    nx *= scale; ny *= scale;
}

void MCP3208ADCAddon::saveCurrentCurveData(int stick) {
    if (stick < 0 || stick >= MCP3208_STICK_COUNT) return;
    temp_curve_storage_[stick].saved_points_count = 0;
    if (adc_pairs_[stick].curve_points_sorted_count > 2) {
        for (int i = 1; i < adc_pairs_[stick].curve_points_sorted_count - 1 && temp_curve_storage_[stick].saved_points_count < 3; i++) {
            temp_curve_storage_[stick].saved_points[temp_curve_storage_[stick].saved_points_count++] = {
                adc_pairs_[stick].curve_points_sorted[i].x,
                adc_pairs_[stick].curve_points_sorted[i].y,
                adc_pairs_[stick].curve_points_sorted[i].buttonMask
            };
        }
    }
    temp_curve_storage_[stick].is_saved = true;
}

void MCP3208ADCAddon::restoreCurveData(int stick) {
    if (stick < 0 || stick >= MCP3208_STICK_COUNT || !temp_curve_storage_[stick].is_saved) return;
    Gamepad* gp = Storage::getInstance().GetGamepad();
    forceReleaseActiveControlPoints(stick, gp);
    if (temp_curve_storage_[stick].saved_points_count > 0)
        initializeCurveSegments(stick, temp_curve_storage_[stick].saved_points, temp_curve_storage_[stick].saved_points_count);
    else {
        adc_pairs_[stick].curve_points_sorted_count = 0;
        adc_pairs_[stick].curve_segments_count = 0;
        adc_pairs_[stick].active_control_points_mask = 0;
    }
    temp_curve_storage_[stick].is_saved = false;
    active_activation_preset_[stick] = 0;
}

void MCP3208ADCAddon::applyPresetCurve(int stick, int preset_index) {
    if (stick < 0 || stick >= MCP3208_STICK_COUNT || preset_index < 0 || preset_index >= 4) return;
    const AnalogOptions& o = Storage::getInstance().getAddonOptions().analogOptions;
    if (preset_index >= (int)o.joystick_curve_presets_count || o.joystick_curve_presets[preset_index].points_count == 0) return;
    const CurvePreset& preset = o.joystick_curve_presets[preset_index];
    Gamepad* gp = Storage::getInstance().GetGamepad();
    forceReleaseActiveControlPoints(stick, gp);
    MCP3208CurvePoint conv[3];
    for (int i = 0; i < (int)preset.points_count && i < 3; i++) {
        conv[i].x = preset.points[i].x;
        conv[i].y = preset.points[i].y;
        conv[i].buttonMask = 0;
    }
    initializeCurveSegments(stick, conv, preset.points_count);
}

// addonKeyboardKeyMask 位与 GpioAction 对应：KEYBOARD_KEY_A=131 → bit0，KEYBOARD_KEY_9=169 → bit38
static constexpr uint32_t CH25_KEYBOARD_KEY_BASE = 131;

static void setCh25MappingFromGpio(VoltageSwitchMap& entry, const GpioMappingInfo& m, uint32_t buttonMask, uint32_t dpadMask) {
    entry.buttonMask = buttonMask;
    entry.dpadMask = dpadMask;
    if (m.action >= GpioAction::KEYBOARD_KEY_A && m.action <= GpioAction::KEYBOARD_KEY_9)
        entry.keyboardKeyBit = static_cast<uint8_t>(static_cast<uint32_t>(m.action) - CH25_KEYBOARD_KEY_BASE);
    else
        entry.keyboardKeyBit = 0xFF;
}

void MCP3208ADCAddon::buildCh25Maps() {
    const FnKeyMappingOptions& fn = Storage::getInstance().getAddonOptions().fnKeyMappingOptions;
    uint32_t b = 0, d = 0;
    // CH2: 左MT, L3, Ext左扳机, 左FN
    ch2_map_[0].threshold = CH25_T1;
    gpioMappingToMasks(fn.leftMtMapping, &b, &d);
    setCh25MappingFromGpio(ch2_map_[0], fn.leftMtMapping, b, d);
    ch2_map_[1].threshold = CH25_T2;
    ch2_map_[1].buttonMask = GAMEPAD_MASK_L3;
    ch2_map_[1].dpadMask = 0;
    ch2_map_[1].keyboardKeyBit = 0xFF;
    ch2_map_[2].threshold = CH25_T3;
    gpioMappingToMasks(fn.leftExtTriggerMapping, &b, &d);
    setCh25MappingFromGpio(ch2_map_[2], fn.leftExtTriggerMapping, b, d);
    ch2_map_[3].threshold = CH25_T4;
    gpioMappingToMasks(fn.leftFnMapping, &b, &d);
    setCh25MappingFromGpio(ch2_map_[3], fn.leftFnMapping, b, d);
    // CH5: 右MT, R3, Ext右扳机, 右FN
    ch5_map_[0].threshold = CH25_T1;
    gpioMappingToMasks(fn.rightMtMapping, &b, &d);
    setCh25MappingFromGpio(ch5_map_[0], fn.rightMtMapping, b, d);
    ch5_map_[1].threshold = CH25_T2;
    ch5_map_[1].buttonMask = GAMEPAD_MASK_R3;
    ch5_map_[1].dpadMask = 0;
    ch5_map_[1].keyboardKeyBit = 0xFF;
    ch5_map_[2].threshold = CH25_T3;
    gpioMappingToMasks(fn.rightExtTriggerMapping, &b, &d);
    setCh25MappingFromGpio(ch5_map_[2], fn.rightExtTriggerMapping, b, d);
    ch5_map_[3].threshold = CH25_T4;
    gpioMappingToMasks(fn.rightFnMapping, &b, &d);
    setCh25MappingFromGpio(ch5_map_[3], fn.rightFnMapping, b, d);
}

void MCP3208ADCAddon::applyCh2Ch5Keys(Gamepad* gamepad) {
    // 用预构建的 threshold 表得到档位，无每帧映射调用
    int cand2 = -1, cand5 = -1;
    uint16_t adc2 = adcValues_[2], adc5 = adcValues_[5];
    for (int i = 0; i < MCP3208_CH25_LEVELS; i++) {
        if (adc2 < ch2_map_[i].threshold) { cand2 = i; break; }
    }
    for (int i = 0; i < MCP3208_CH25_LEVELS; i++) {
        if (adc5 < ch5_map_[i].threshold) { cand5 = i; break; }
    }

    auto updateDebounce = [](int candidate, int8_t& stable_level, int8_t& pending_level, uint8_t& debounce_count) {
        if (candidate == stable_level) {
            debounce_count = 0;
            return;
        }
        if (candidate == pending_level) {
            debounce_count++;
            if (debounce_count >= CH25_DEBOUNCE_FRAMES) {
                stable_level = pending_level;
                debounce_count = 0;
            }
        } else {
            pending_level = static_cast<int8_t>(candidate);
            debounce_count = 1;
        }
    };
    updateDebounce(cand2, ch2_stable_level_, ch2_pending_level_, ch2_debounce_count_);
    updateDebounce(cand5, ch5_stable_level_, ch5_pending_level_, ch5_debounce_count_);
    int l2 = ch2_stable_level_, l5 = ch5_stable_level_;

    // 只清除本插件上一帧输出的 mask，不清除「所有档位可能用到的按键」并集，避免覆盖触摸板/GPIO 等同按键
    gamepad->state.buttons &= ~(last_ch2_buttons_ | last_ch5_buttons_);
    gamepad->state.dpad   &= ~(last_ch2_dpad_   | last_ch5_dpad_);
    gamepad->addonKeyboardKeyMask &= ~(last_ch2_keyboard_ | last_ch5_keyboard_);
    uint32_t curr2_btn = 0, curr2_dpad = 0;
    uint64_t curr2_kb = 0;
    uint32_t curr5_btn = 0, curr5_dpad = 0;
    uint64_t curr5_kb = 0;
    if (l2 >= 0) {
        curr2_btn = ch2_map_[l2].buttonMask;
        curr2_dpad = ch2_map_[l2].dpadMask;
        if (ch2_map_[l2].keyboardKeyBit != 0xFF) curr2_kb = (1ULL << ch2_map_[l2].keyboardKeyBit);
        gamepad->state.buttons |= curr2_btn;
        gamepad->state.dpad   |= curr2_dpad;
        gamepad->addonKeyboardKeyMask |= curr2_kb;
    }
    if (l5 >= 0) {
        curr5_btn = ch5_map_[l5].buttonMask;
        curr5_dpad = ch5_map_[l5].dpadMask;
        if (ch5_map_[l5].keyboardKeyBit != 0xFF) curr5_kb = (1ULL << ch5_map_[l5].keyboardKeyBit);
        gamepad->state.buttons |= curr5_btn;
        gamepad->state.dpad   |= curr5_dpad;
        gamepad->addonKeyboardKeyMask |= curr5_kb;
    }
    last_ch2_buttons_ = curr2_btn;
    last_ch2_dpad_    = curr2_dpad;
    last_ch2_keyboard_ = curr2_kb;
    last_ch5_buttons_ = curr5_btn;
    last_ch5_dpad_    = curr5_dpad;
    last_ch5_keyboard_ = curr5_kb;
}

void MCP3208ADCAddon::process() {
    if (!spiOk_) return;
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    Gamepad* gamepad = Storage::getInstance().GetGamepad();

    if (analogOptions.joystick_curve_enabled) {
        int stick_num = 1;
        bool found = false;
        int pressed_idx = -1;
        for (int pi = 0; pi < 4 && pi < (int)analogOptions.joystick_curve_presets_count; pi++) {
            uint32_t am = analogOptions.joystick_curve_presets[pi].activationButtonMask;
            if (am == 0) continue;
            uint32_t dpad_m = am & (GAMEPAD_MASK_DU|GAMEPAD_MASK_DD|GAMEPAD_MASK_DL|GAMEPAD_MASK_DR);
            uint32_t reg_m = am & ~(GAMEPAD_MASK_DU|GAMEPAD_MASK_DD|GAMEPAD_MASK_DL|GAMEPAD_MASK_DR);
            bool on = (reg_m && (gamepad->state.buttons & reg_m) == reg_m) ||
                (dpad_m & GAMEPAD_MASK_DU && (gamepad->state.dpad & GAMEPAD_MASK_UP)) ||
                (dpad_m & GAMEPAD_MASK_DD && (gamepad->state.dpad & GAMEPAD_MASK_DOWN)) ||
                (dpad_m & GAMEPAD_MASK_DL && (gamepad->state.dpad & GAMEPAD_MASK_LEFT)) ||
                (dpad_m & GAMEPAD_MASK_DR && (gamepad->state.dpad & GAMEPAD_MASK_RIGHT));
            if (on) { found = true; pressed_idx = pi; break; }
        }
        if (found) {
            if (active_activation_preset_[stick_num] == 0) {
                saveCurrentCurveData(stick_num);
                if (pressed_idx < (int)analogOptions.joystick_curve_presets_count &&
                    analogOptions.joystick_curve_presets[pressed_idx].points_count > 0) {
                    applyPresetCurve(stick_num, pressed_idx);
                    active_activation_preset_[stick_num] = (uint8_t)(pressed_idx + 1);
                }
            }
        } else {
            if (active_activation_preset_[stick_num] != 0) restoreCurveData(stick_num);
        }
        uint32_t p1 = analogOptions.curve_profile_1, p2 = analogOptions.curve_profile_2;
        if (usage_curve_profile_1_ != p1 || usage_curve_profile_2_ != p2) {
            usage_curve_profile_1_ = p1; usage_curve_profile_2_ = p2;
            reinit();
        }
    }

    // 与模拟摇杆一致：按当前模式输出 16bit 范围。12bit ADC 经归一化到 [0,1] 后乘 joystickMax
    // XInput 等模式：driver->GetJoystickMidValue()=32767，joystickMax=65535
    // 每帧只取一次 driver，不缓存在 setup()：若将来支持运行时切换输入模式，缓存的指针会失效
    uint32_t joystickMax = GAMEPAD_JOYSTICK_MAX;
    GPDriver* driver = DriverManager::getInstance().getDriver();
    if (driver != nullptr) {
        joystickMax = driver->GetJoystickMidValue() * 2;
    }

    for (int i = 0; i < MCP3208_STICK_COUNT; i++) {
        // Step 1: Read raw ADC (with jitter filter) and transform to center-relative coordinates
        float cx = getStickRaw(i, true) - (float)adc_pairs_[i].x_center;
        float cy = getStickRaw(i, false) - (float)adc_pairs_[i].y_center;

        // Step 2: Range calibration scaling (radial scaling), scale always > 0 (0.65 when uncalibrated)
        float scale = getInterpolatedScale(i, std::atan2(cy, cx));
        float sx = cx / scale;
        float sy = cy / scale;

        // Step 3: Normalize to [-1, 1] and apply inversion (match analog)
        float nx = sx / MCP3208_ADC_MAX_HALF;
        float ny = sy / MCP3208_ADC_MAX_HALF;
        if (adc_pairs_[i].analog_invert == InvertMode::INVERT_X || adc_pairs_[i].analog_invert == InvertMode::INVERT_XY) nx = -nx;
        if (adc_pairs_[i].analog_invert == InvertMode::INVERT_Y || adc_pairs_[i].analog_invert == InvertMode::INVERT_XY) ny = -ny;

        // Step 4: Apply deadzone and anti-deadzone (match analog)
        float dist_sq = nx * nx + ny * ny;
        float deadzone_sq = adc_pairs_[i].in_deadzone * adc_pairs_[i].in_deadzone;
        if (dist_sq < deadzone_sq) {
            nx = 0.0f;
            ny = 0.0f;
            dist_sq = 0.0f;
        } else if (adc_pairs_[i].anti_deadzone > 0.0f) {
            float dist = std::sqrt(dist_sq);
            float baseline = adc_pairs_[i].anti_deadzone;
            if (adc_pairs_[i].fixed_anti_deadzone) {
                if (dist > 0.0f && dist < baseline) {
                    float scale_factor = baseline / dist;
                    nx *= scale_factor;
                    ny *= scale_factor;
                }
            } else {
                if (dist > 0.0f) {
                    float new_dist = dist + baseline;
                    float scale_factor = new_dist / dist;
                    nx *= scale_factor;
                    ny *= scale_factor;
                }
            }
        }

        // Step 5: Square trimming (DS4-style) - clamp to [-1, 1]
        nx = std::clamp(nx, -1.0f, 1.0f);
        ny = std::clamp(ny, -1.0f, 1.0f);

        // Step 6: Apply response curve if configured (match analog)
        if (adc_pairs_[i].curve_points_sorted_count > 0 || active_activation_preset_[i] != 0) {
            applyResponseCurveToCoordinates(nx, ny, i, gamepad);
        }

        // Final: Convert from [-1, 1] to [0, 1] for storage and output (match analog)
        float x_value = nx * 0.5f + MCP3208_ANALOG_CENTER;
        float y_value = ny * 0.5f + MCP3208_ANALOG_CENTER;
        adc_pairs_[i].x_value = x_value;
        adc_pairs_[i].y_value = y_value;

        float clamped_x = std::clamp(x_value, 0.0f, 1.0f);
        float clamped_y = std::clamp(y_value, 0.0f, 1.0f);
        uint16_t clampedX = (uint16_t)std::min((uint32_t)(joystickMax * clamped_x), (uint32_t)0xFFFF);
        uint16_t clampedY = (uint16_t)std::min((uint32_t)(joystickMax * clamped_y), (uint32_t)0xFFFF);
        if (adc_pairs_[i].analog_dpad == DpadMode::DPAD_MODE_LEFT_ANALOG) {
            gamepad->state.lx = clampedX;
            gamepad->state.ly = clampedY;
        } else if (adc_pairs_[i].analog_dpad == DpadMode::DPAD_MODE_RIGHT_ANALOG) {
            gamepad->state.rx = clampedX;
            gamepad->state.ry = clampedY;
        }
    }

    applyCh2Ch5Keys(gamepad);
}

void MCP3208ADCAddon::reinit() {
    const AnalogOptions& o = Storage::getInstance().getAddonOptions().analogOptions;
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    forceReleaseActiveControlPoints(0, gamepad);
    forceReleaseActiveControlPoints(1, gamepad);
    // Reinitialize curve segments only (match analog: no hardware/jitter re-init here)
    bool curveEnabled = o.joystick_curve_enabled;
    for (int i = 0; i < MCP3208_STICK_COUNT; i++) {
        adc_pairs_[i].curve_points_sorted_count = 0;
        adc_pairs_[i].curve_segments_count = 0;
        adc_pairs_[i].active_control_points_mask = 0;
        if (curveEnabled && (i == 0 ? o.joystick_curve_points_1_count : o.joystick_curve_points_2_count) > 0) {
            MCP3208CurvePoint conv[3];
            if (i == 0)
                convertCurvePoints(o.joystick_curve_points_1, o.joystick_curve_points_1_count, conv);
            else
                convertCurvePoints(o.joystick_curve_points_2, o.joystick_curve_points_2_count, conv);
            initializeCurveSegments(i, conv, i == 0 ? o.joystick_curve_points_1_count : o.joystick_curve_points_2_count);
        }
    }
    buildCh25Maps();
}
