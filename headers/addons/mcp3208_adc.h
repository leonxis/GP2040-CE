#ifndef _MCP3208_ADC_H
#define _MCP3208_ADC_H

#include "gpaddon.h"
#include "BoardConfig.h"
#include "GamepadEnums.h"
#include "enums.pb.h"
#include "types.h"

#ifndef MCP3208_ADC_ENABLED
#define MCP3208_ADC_ENABLED 0
#endif

#define MCP3208_ADC_ADDON_NAME "MCP3208 ADC"

// SPI 固定：GPIO0=RX(MISO), GPIO2=SCK, GPIO3=TX(MOSI)；MCP3208 CS=GPIO1；1.5MHz Mode0
#define MCP3208_SPI_BLOCK_ID    0
#define MCP3208_SPI_RX_PIN      0
#define MCP3208_SPI_SCK_PIN     2
#define MCP3208_SPI_TX_PIN      3
#define MCP3208_CS_PIN          1
#define MCP3208_SPI_HZ          1500000u

// 通道：CH0=左X, CH1=左Y, CH6=右Y, CH7=右X；CH2/CH5=四档开关；CH3/CH4 悬空不读
#define MCP3208_READ_CHANNELS    6   // 0,1,2,5,6,7
#define MCP3208_ADC_MAX         4095
#define MCP3208_ADC_MAX_HALF    (MCP3208_ADC_MAX * 0.5f)
#define MCP3208_STICK_COUNT     2
#define MCP3208_CIRCULARITY_SIZE 48
#define MCP3208_ANALOG_CENTER   0.5f

// 与 Analog 插件相同的摇杆曲线/校准结构（复刻）
typedef struct {
    float x;
    float y;
    uint32_t buttonMask;
} MCP3208CurvePoint;

typedef struct {
    float x_value;
    float y_value;
    uint16_t x_center;
    uint16_t y_center;
    InvertMode analog_invert;
    DpadMode analog_dpad;
    float in_deadzone;
    float anti_deadzone;
    bool fixed_anti_deadzone;
    uint32_t jitter_filter;   // ADC units, 0 = disabled (match analog)
    uint16_t last_x_adc;
    uint16_t last_y_adc;
    float range_data[MCP3208_CIRCULARITY_SIZE];
    bool has_range_calibration;
    bool finetune_shape_force_circular;
    float finetune_shape_amplify;
    struct {
        float x;
        float y;
        uint32_t buttonMask;
    } curve_points_sorted[5];
    uint8_t curve_points_sorted_count;
    struct {
        float slope;
        float intercept;
        float x_start;
        float x_end;
    } curve_segments[4];
    uint8_t curve_segments_count;
    uint8_t active_control_points_mask;
} MCP3208StickInstance;

class MCP3208ADCAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void preprocess();
    virtual void process();
    virtual void postprocess(bool) {}
    virtual std::string name() { return MCP3208_ADC_ADDON_NAME; }
    virtual void reinit();

private:
    void readAllChannels();
    float getStickRaw(int stick, bool isX);  // non-const: applies jitter and updates last_x/y_adc
    float getInterpolatedScale(int stick, float angle) const;
    void applyFinetuneShapeAdjustments(int stick);
    void initializeCurveSegments(int stick, const MCP3208CurvePoint* points, int count);
    void applyResponseCurveToCoordinates(float& nx, float& ny, int stick, class Gamepad* gamepad);
    void forceReleaseActiveControlPoints(int stick, class Gamepad* gamepad);
    void saveCurrentCurveData(int stick);
    void restoreCurveData(int stick);
    void applyPresetCurve(int stick, int preset_index);
    void applyCh2Ch5Keys(class Gamepad* gamepad);

    PeripheralSPI* spi_;
    uint16_t adcValues_[8];   // CH0-CH7，仅 0,1,2,5,6,7 有效
    bool spiOk_;
    MCP3208StickInstance adc_pairs_[MCP3208_STICK_COUNT];
    uint32_t usage_curve_profile_1_;
    uint32_t usage_curve_profile_2_;
    struct {
        bool is_saved;
        MCP3208CurvePoint saved_points[3];
        uint8_t saved_points_count;
    } temp_curve_storage_[MCP3208_STICK_COUNT];
    uint8_t active_activation_preset_[MCP3208_STICK_COUNT];
    // CH2/CH5 四档开关多帧防抖：连续 N 帧同档位才更新，N = CH25_DEBOUNCE_FRAMES（见 .cpp 顶部）
    int8_t ch2_stable_level_;
    int8_t ch2_pending_level_;
    uint8_t ch2_debounce_count_;
    int8_t ch5_stable_level_;
    int8_t ch5_pending_level_;
    uint8_t ch5_debounce_count_;
};

#endif
