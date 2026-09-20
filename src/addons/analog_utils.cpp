#include "addons/analog_utils.h"
#include "addons/mcp3208_adc.h"
#include "storagemanager.h"
#include "eventmanager.h"

// 仅支持 MCP3208 外置 ADC 作为摇杆数据源（RP2354B 片上 ADC 基址为 GPIO40，
// 不再提供片上 ADC 回落路径）
bool readJoystickADC(uint8_t stickNum, uint32_t& x, uint32_t& y, uint32_t& adcMax) {
    x = 0;
    y = 0;
    adcMax = 0;
    return MCP3208ADCAddon::getRawStickForWebConfig(stickNum, x, y, adcMax);
}

void calculateCalibrationCenter(const uint16_t values[8], uint32_t& avgX, uint32_t& avgY) {
    // Calculate average from four points: [x1, y1, x2, y2, x3, y3, x4, y4]
    // Average X: (x1 + x2 + x3 + x4) / 4
    // Average Y: (y1 + y2 + y3 + y4) / 4
    avgX = (values[0] + values[2] + values[4] + values[6]) / 4;
    avgY = (values[1] + values[3] + values[5] + values[7]) / 4;
}

void saveCalibrationValues(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2) {
    AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;

    // Save calibration values
    analogOptions.joystick_center_x = x1;
    analogOptions.joystick_center_y = y1;
    analogOptions.joystick_center_x2 = x2;
    analogOptions.joystick_center_y2 = y2;

    // Auto calibration removed - manual calibration values are always used

    // Save to flash
    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true, false));
}
