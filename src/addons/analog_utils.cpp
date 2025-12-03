#include "addons/analog_utils.h"
#include "addons/analog.h"  // For ADC_PIN_OFFSET definition
#include "storagemanager.h"
#include "eventmanager.h"
#include "hardware/adc.h"
#include "helper.h"

// Use ADC_PIN_OFFSET from analog.cpp
// Note: This is defined in analog.cpp, but we need it here
// We'll use the same value to maintain consistency
#ifndef ADC_PIN_OFFSET
#define ADC_PIN_OFFSET 26
#endif

bool readJoystickADC(uint8_t stickNum, uint16_t& x, uint16_t& y) {
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    x = 0;
    y = 0;
    
    if (!analogOptions.enabled) {
        return false;
    }
    
    // Initialize ADC if not already initialized
    adc_init();
    
    // Get pin configuration for the requested stick
    Pin_t xPin = (stickNum == 0) ? analogOptions.analogAdc1PinX : analogOptions.analogAdc2PinX;
    Pin_t yPin = (stickNum == 0) ? analogOptions.analogAdc1PinY : analogOptions.analogAdc2PinY;
    
    // Read X pin
    if (isValidPin(xPin)) {
        adc_gpio_init(xPin);
        adc_select_input(xPin - ADC_PIN_OFFSET);
        x = adc_read();
    }
    
    // Read Y pin
    if (isValidPin(yPin)) {
        adc_gpio_init(yPin);
        adc_select_input(yPin - ADC_PIN_OFFSET);
        y = adc_read();
    }
    
    return true;
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

