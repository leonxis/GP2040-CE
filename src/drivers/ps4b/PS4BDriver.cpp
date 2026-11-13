#include "drivers/ps4b/PS4BDriver.h"
#include "drivers/shared/driverhelper.h"
#include "storagemanager.h"
#include "usbdriver.h"
#include <string.h>

#include "enums.pb.h"

// Note: PS4B mode uses PC host mode (PS4_ID_EMULATION), which does not require PS4 console authentication
// Therefore, PS4Auth, mbedtls, and CRC32 are not needed for PS4B mode

// Controller calibration
static constexpr uint8_t output_0x02[] = {
    0xfe, 0xff, 0x0e, 0x00, 0x04, 0x00, 0xd4, 0x22,
    0x2a, 0xdd, 0xbb, 0x22, 0x5e, 0xdd, 0x81, 0x22, 
    0x84, 0xdd, 0x1c, 0x02, 0x1c, 0x02, 0x85, 0x1f,
    0xb0, 0xe0, 0xc6, 0x20, 0xb5, 0xe0, 0xb1, 0x20,
    0x83, 0xdf, 0x0c, 0x00
};

// Controller descriptor (byte[4] = 0x00 for ps4, 0x07 for ps5)
static constexpr uint8_t output_0x03[] = {
//--------------------------------------------------
// Use for normal controlller support.
//--------------------------------------------------
//    0x21, 0x27, 0x04, 0xcf, 0x00, 0x2c, 0x56,
//    0x08, 0x00, 0x3d, 0x00, 0xe8, 0x03, 0x04, 0x00,
//    0xff, 0x7f, 0x0d, 0x0d, 0x00, 0x00, 0x00, 0x00,
//    0x0D, 0x84, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
//    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
//    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

//--------------------------------------------------
// Use for wheel support. What's different?
//--------------------------------------------------
    0x21, 0x27, 0x03, 0x11, 0x06, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0D, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x0D, 0x84, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Bluetooth device and host details
static constexpr uint8_t output_0x12[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // device MAC address
    0x08, 0x25, 0x00,                   // BT device class
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // host MAC address
};

// Controller firmware version and datestamp
static constexpr uint8_t output_0xa3[] = {
    0x4a, 0x75, 0x6e, 0x20, 0x20, 0x39, 0x20, 0x32,
    0x30, 0x31, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x31, 0x32, 0x3a, 0x33, 0x36, 0x3a, 0x34, 0x31,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x08, 0xb4, 0x01, 0x00, 0x00, 0x00,
    0x07, 0xa0, 0x10, 0x20, 0x00, 0xa0, 0x02, 0x00
};

// Nonce Page Size: 0x38 (56)
// Response Page Size: 0x38 (56)
static constexpr uint8_t output_0xf3[] = {
    0x0, 0x38, 0x38, 0, 0, 0, 0
};

void PS4BDriver::initialize() {
    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    const GamepadOptions & options = gamepad->getOptions();

    // set up device descriptor IDs depending on mode
    uint8_t descSize = sizeof(ps4_device_descriptor);
    memcpy(deviceDescriptor, &ps4_device_descriptor, descSize);

    memset(&ps4Features, 0, sizeof(ps4Features));

    // PS4B mode always uses PS4_ID_EMULATION (PC host mode)
    bool isDeviceEmulated = true; // Fixed to PC host mode

    //deviceType = InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_GAMEPAD;
    deviceType = options.inputDeviceType;

    if (!isDeviceEmulated) {
        if (deviceType == InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_WHEEL) {
            // wheel
            deviceDescriptor[8] = LSB(PS4_WHEEL_VENDOR_ID);
            deviceDescriptor[9] = MSB(PS4_WHEEL_VENDOR_ID);
            deviceDescriptor[10] = LSB(PS4_WHEEL_PRODUCT_ID);
            deviceDescriptor[11] = MSB(PS4_WHEEL_PRODUCT_ID);
        } else {
            // assume gamepad if not special cased
            deviceDescriptor[8] = LSB(PS4_VENDOR_ID);
            deviceDescriptor[9] = MSB(PS4_VENDOR_ID);
            deviceDescriptor[10] = LSB(PS4_PRODUCT_ID);
            deviceDescriptor[11] = MSB(PS4_PRODUCT_ID);
        }
    } else {
        deviceDescriptor[8] = LSB(DS4_VENDOR_ID);
        deviceDescriptor[9] = MSB(DS4_VENDOR_ID);
        deviceDescriptor[10] = LSB(DS4_PRODUCT_ID);
        deviceDescriptor[11] = MSB(DS4_PRODUCT_ID);
    }

    // check InputModeDeviceType mode for PS4 driver and set PS4ControllerType where applicable
    // PS5 mode currently forces PS4ControllerType 7 (PS4_ARCADESTICK) for PS5 compatibility
    switch (deviceType) {
        case InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_WHEEL:
            enableController = false;
            enableMotion = false;
            enableLED = false;
            enableRumble = false;
            enableAnalog = true;
            enableUnknown0 = false;
            enableTouchpad = false;
            enableUnknown1 = false;
            shifterPosition = 0;

            controllerType = PS4ControllerType::PS4_WHEEL;

            buttonShiftUp = new GamepadButtonMapping(0);
            buttonShiftDown = new GamepadButtonMapping(0);
            buttonShift1 = new GamepadButtonMapping(0);
            buttonShift2 = new GamepadButtonMapping(0);
            buttonShift3 = new GamepadButtonMapping(0);
            buttonShift4 = new GamepadButtonMapping(0);
            buttonShift5 = new GamepadButtonMapping(0);
            buttonShift6 = new GamepadButtonMapping(0);
            buttonShiftR = new GamepadButtonMapping(0);
            buttonShiftN = new GamepadButtonMapping(0);
            buttonGas = new GamepadButtonMapping(0);
            buttonBrake = new GamepadButtonMapping(0);
            buttonClutch = new GamepadButtonMapping(0);
            buttonSteerLeft = new GamepadButtonMapping(0);
            buttonSteerRight = new GamepadButtonMapping(0);
            buttonPlus = new GamepadButtonMapping(0);
            buttonMinus = new GamepadButtonMapping(0);
            buttonDialDown = new GamepadButtonMapping(0);
            buttonDialUp = new GamepadButtonMapping(0);
            buttonDialEnter = new GamepadButtonMapping(0);
            break;
        case InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_HOTAS:
            enableController = false;
            enableMotion = false;
            enableLED = false;
            enableRumble = false;
            enableAnalog = true;
            enableUnknown0 = false;
            enableTouchpad = false;
            enableUnknown1 = false;

            controllerType = PS4ControllerType::PS4_HOTAS;

            buttonRudderLeft = new GamepadButtonMapping(0);
            buttonRudderRight = new GamepadButtonMapping(0);
            buttonThrottleForward = new GamepadButtonMapping(0);
            buttonThrottleReverse = new GamepadButtonMapping(0);
            buttonRockerLeft = new GamepadButtonMapping(0);
            buttonRockerRight = new GamepadButtonMapping(0);
            buttonPedalLeft = new GamepadButtonMapping(0);
            buttonPedalRight = new GamepadButtonMapping(0);
            buttonPedalRudderLeft = new GamepadButtonMapping(0);
            buttonPedalRudderRight = new GamepadButtonMapping(0);
            break;
        case InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_GUITAR:
            enableController = true;
            enableMotion = false;
            enableLED = false;
            enableRumble = false;
            enableAnalog = true;
            enableUnknown0 = false;
            enableTouchpad = false;
            enableUnknown1 = true;

            controllerType = PS4ControllerType::PS4_GUITAR;

            buttonFretGreen = new GamepadButtonMapping(0);
            buttonFretRed = new GamepadButtonMapping(0);
            buttonFretYellow = new GamepadButtonMapping(0);
            buttonFretBlue = new GamepadButtonMapping(0);
            buttonFretOrange = new GamepadButtonMapping(0);
            buttonFretSoloGreen = new GamepadButtonMapping(0);
            buttonFretSoloRed = new GamepadButtonMapping(0);
            buttonFretSoloYellow = new GamepadButtonMapping(0);
            buttonFretSoloBlue = new GamepadButtonMapping(0);
            buttonFretSoloOrange = new GamepadButtonMapping(0);
            buttonWhammy = new GamepadButtonMapping(0);
            buttonPickup = new GamepadButtonMapping(0);
            buttonTilt = new GamepadButtonMapping(0);
            break;
        case InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_DRUM:
            enableController = true;
            enableMotion = false;
            enableLED = false;
            enableRumble = false;
            enableAnalog = true;
            enableUnknown0 = false;
            enableTouchpad = false;
            enableUnknown1 = true;

            controllerType = PS4ControllerType::PS4_DRUMS;

            buttonDrumPadRed = new GamepadButtonMapping(0);
            buttonDrumPadBlue = new GamepadButtonMapping(0);
            buttonDrumPadYellow = new GamepadButtonMapping(0);
            buttonDrumPadGreen = new GamepadButtonMapping(0);
            buttonCymbalYellow = new GamepadButtonMapping(0);
            buttonCymbalBlue = new GamepadButtonMapping(0);
            buttonCymbalGreen = new GamepadButtonMapping(0);
            buttonKickPedalLeft = new GamepadButtonMapping(0);
            buttonKickPedalRight = new GamepadButtonMapping(0);
            break;
        case InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_GAMEPAD:
        default:
            enableController = true;
            enableMotion = true;
            enableLED = true;
            enableRumble = true;
            enableAnalog = false;
            enableUnknown0 = true;
            enableTouchpad = true;
            enableUnknown1 = true;

            // PS4B mode always uses PS4ControllerType::PS4_CONTROLLER (same as PS4 mode)
            // PS4B is based on PS4 PC host mode, so it should use PS4_CONTROLLER
            controllerType = PS4ControllerType::PS4_CONTROLLER;
            break;
    }

    // init feature data
    if (deviceType == InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_GAMEPAD) {
        touchpadData.p1.unpressed = 1;
        touchpadData.p1.set_x(PS4_TP_X_MAX / 2);
        touchpadData.p1.set_y(PS4_TP_Y_MAX / 2);
        touchpadData.p2.unpressed = 1;
        touchpadData.p2.set_x(PS4_TP_X_MAX / 2);
        touchpadData.p2.set_y(PS4_TP_Y_MAX / 2);

        sensorData.powerLevel = 0xB; // 0x00-0x0A, 0x00-0x0B if charging
        sensorData.charging = 1;     // set this to 1 to show as plugged in
        sensorData.headphones = 0;
        sensorData.microphone = 0;
        sensorData.extension = 0;
        sensorData.temperature = 0;
        memset(sensorData.extData, 0, sizeof(sensorData.extData));
        sensorData.gyroX = 0;
        sensorData.gyroY = 0;
        sensorData.gyroZ = 0;
        sensorData.accelX = 0;
        sensorData.accelY = 0;
        sensorData.accelZ = 0;
    }

    // preseed touchpad sensors with center position values
    gamepad->auxState.sensors.touchpad[0].x = PS4_TP_X_MAX/2;
    gamepad->auxState.sensors.touchpad[0].y = PS4_TP_Y_MAX/2;
    gamepad->auxState.sensors.touchpad[1].x = PS4_TP_X_MAX/2;
    gamepad->auxState.sensors.touchpad[1].y = PS4_TP_Y_MAX/2;

    touchCounter = 0;

    ps4Report = {
        .reportID = 0x01,
        .leftStickX = PS4_JOYSTICK_MID,
        .leftStickY = PS4_JOYSTICK_MID,
        .rightStickX = PS4_JOYSTICK_MID,
        .rightStickY = PS4_JOYSTICK_MID,
        .dpad = 0x08,
        .buttonWest = 0, .buttonSouth = 0, .buttonEast = 0, .buttonNorth = 0,
        .buttonL1 = 0, .buttonR1 = 0, .buttonL2 = 0, .buttonR2 = 0,
        .buttonSelect = 0, .buttonStart = 0, .buttonL3 = 0, .buttonR3 = 0, .buttonHome = 0, .buttonTouchpad = 0,
        .reportCounter = 0, .leftTrigger = 0, .rightTrigger = 0,
        .miscData = {}
    };
    
    // see output_0x03
    controllerConfig = {
        .hidUsage = 0x2721,
        .mystery0 = 0x04,
//        .featureValue = 0xEF,
        .features = {
            .enableController = enableController,
            .enableMotion = enableMotion,
            .enableLED = enableLED,
            .enableRumble = enableRumble,
            .enableAnalog = enableAnalog,
            .enableUnknown0 = enableUnknown0,
            .enableTouchpad = enableTouchpad,
            .enableUnknown1 = enableUnknown1,
        },
        .controllerType = (uint8_t)controllerType,
        .touchpadParam = {0x2c, 0x56},
        .imuConfig = {
            .gyroRange = 0x0008,
            .gyroResPerDegDenom = 0x003D,
            .gyroResPerDegNumer = 0x03E8,
            .accelRange = 0x0004,
            .accelResPerG = 0x7FFF,
        },
        .magicID = 0x0d0d,
        .mystery1 = {},
        .wheelParam = {0x0D, 0x84, 0x03},
        .mystery2 = {0x00}
    };

    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        switch (pinMappings[pin].action) {
            case GpioAction::MODE_GUITAR_FRET_GREEN: buttonFretGreen->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_RED: buttonFretRed->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_YELLOW: buttonFretYellow->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_BLUE: buttonFretBlue->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_ORANGE: buttonFretOrange->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_SOLO_GREEN: buttonFretSoloGreen->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_SOLO_RED: buttonFretSoloRed->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_SOLO_YELLOW: buttonFretSoloYellow->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_SOLO_BLUE: buttonFretSoloBlue->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_SOLO_ORANGE: buttonFretSoloOrange->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_WHAMMY: buttonWhammy->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_PICKUP: buttonPickup->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_TILT: buttonTilt->pinMask |= 1 << pin; break;

            case GpioAction::MODE_DRUM_RED_DRUMPAD: buttonDrumPadRed->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_BLUE_DRUMPAD: buttonDrumPadBlue->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_YELLOW_DRUMPAD: buttonDrumPadYellow->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_GREEN_DRUMPAD: buttonDrumPadGreen->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_YELLOW_CYMBAL: buttonCymbalYellow->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_BLUE_CYMBAL: buttonCymbalBlue->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_GREEN_CYMBAL: buttonCymbalGreen->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_KICK_PEDAL_LEFT: buttonKickPedalLeft->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_KICK_PEDAL_RIGHT: buttonKickPedalRight->pinMask |= 1 << pin; break;

            case GpioAction::MODE_HOTAS_RUDDER_LEFT: buttonRudderLeft->pinMask |= 1 << pin; break;
            case GpioAction::MODE_HOTAS_RUDDER_RIGHT: buttonRudderRight->pinMask |= 1 << pin; break;
            case GpioAction::MODE_HOTAS_THROTTLE_FORWARD: buttonThrottleForward->pinMask |= 1 << pin; break;
            case GpioAction::MODE_HOTAS_THROTTLE_REVERSE: buttonThrottleReverse->pinMask |= 1 << pin; break;
            case GpioAction::MODE_HOTAS_ROCKER_LEFT: buttonRockerLeft->pinMask |= 1 << pin; break;
            case GpioAction::MODE_HOTAS_ROCKER_RIGHT: buttonRockerRight->pinMask |= 1 << pin; break;
            case GpioAction::MODE_HOTAS_PEDAL_LEFT: buttonPedalLeft->pinMask |= 1 << pin; break;
            case GpioAction::MODE_HOTAS_PEDAL_RIGHT: buttonPedalRight->pinMask |= 1 << pin; break;
            case GpioAction::MODE_HOTAS_PEDAL_RUDDER_LEFT: buttonPedalRudderLeft->pinMask |= 1 << pin; break;
            case GpioAction::MODE_HOTAS_PEDAL_RUDDER_RIGHT: buttonPedalRudderRight->pinMask |= 1 << pin; break;

            case GpioAction::MODE_WHEEL_SHIFTER_GEAR_1: buttonShift1->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_SHIFTER_GEAR_2: buttonShift2->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_SHIFTER_GEAR_3: buttonShift3->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_SHIFTER_GEAR_4: buttonShift4->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_SHIFTER_GEAR_5: buttonShift5->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_SHIFTER_GEAR_6: buttonShift6->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_SHIFTER_GEAR_R: buttonShiftR->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_SHIFTER_GEAR_N: buttonShiftN->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_SHIFTER_GEAR_UP: buttonShiftUp->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_SHIFTER_GEAR_DOWN: buttonShiftDown->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_STEERING_LEFT: buttonSteerLeft->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_STEERING_RIGHT: buttonSteerRight->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_BUTTON_PLUS: buttonPlus->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_BUTTON_MINUS: buttonMinus->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_DIAL_UP: buttonDialUp->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_DIAL_DOWN: buttonDialDown->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_DIAL_ENTER: buttonDialEnter->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_PEDAL_GAS: buttonGas->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_PEDAL_BRAKE: buttonBrake->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_PEDAL_CLUTCH: buttonClutch->pinMask |= 1 << pin; break;
            default:    break;
        }
    }

    class_driver = {
    #if CFG_TUSB_DEBUG >= 2
        .name = "PS4",
    #endif
        .init = hidd_init,
        .reset = usb_main_gamepad_hid_reset,
        .open = hidd_open,
        .control_xfer_cb = hidd_control_xfer_cb,
        .xfer_cb = hidd_xfer_cb,
        .sof = NULL
    };

    last_report_counter = 0; // PS4 Reports
    pending_report_counter = 0;
    last_axis_counter = 0;
    pending_axis_timing = 0;
    input_report_pending = false;
    last_report_timer = to_ms_since_boot(get_absolute_time());
    // PS4B mode doesn't need authentication, so no need to initialize nonce variables
}

void PS4BDriver::initializeAux() {
    // PS4B mode uses PS4_ID_EMULATION (PC host mode), which does not require PS4 console authentication
    // No authentication driver needed
    ps4AuthDriver = nullptr;
    ps4AuthData = nullptr;
    authsent = false;
}

bool PS4BDriver::getDongleAuthRequired() {
    // PS4B mode uses PC host mode, no dongle authentication required
    return false;
}

bool PS4BDriver::reportReady() {
    return tud_hid_n_ready(GAMEPAD_INTERFACE);
}

bool PS4BDriver::sendReport(void * report, uint16_t report_size) {
    return tud_hid_n_report(GAMEPAD_INTERFACE, 0, report, report_size);
}

void PS4BDriver::onInputReportComplete() {
    input_report_pending = false;
}

void PS4BDriver::onInputReportFailed() {
    if (!input_report_pending) {
        return;
    }

    last_report_counter = pending_report_counter;
    ps4Report.reportCounter = pending_report_counter;
    if (deviceType == InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_GAMEPAD) {
        ps4Report.gamepad.axisTiming = pending_axis_timing;
    }
    input_report_pending = false;
}

// Called by Core1, PS4 key signing will lock the CPU
void PS4BDriver::processAux() {
    // PS4B mode uses PC host mode, no authentication processing needed
    // Empty function - no authentication required for PS4B mode
}

USBListener * PS4BDriver::get_usb_auth_listener() {
    // PS4B mode uses PC host mode, no USB authentication listener needed
    return nullptr;
}

// tud_hid_get_report_cb
uint16_t PS4BDriver::get_report(uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    // For PS4B, only the gamepad HID base interface is served here.
    if (report_type != HID_REPORT_TYPE_FEATURE) {
        memcpy(buffer, &ps4Report, sizeof(ps4Report));
        return sizeof(ps4Report);
    }

    // DS4 standard Feature Report 0x04 (36 bytes) and 0x88 (63 bytes) — always respond
    if ( report_id == 0x04 ) {
        if ( reqlen < 36 )
            return (uint16_t)-1;
        memset(buffer, 0, 36);
        return 36;
    }
    if ( report_id == 0x88 ) {
        if ( reqlen < 63 )
            return (uint16_t)-1;
        memset(buffer, 0, 63);
        return 63;
    }

    // PS4B mode uses PC host mode - only handle basic feature reports that PC may query
    // Authentication-related reports (SIGNATURE_NONCE, SIGNING_STATE, RESET_AUTH) are not needed
    switch(report_id) {
        // Basic controller reports that PC host may query
        case PS4AuthReport::PS4_GET_CALIBRATION:
            if (reqlen < sizeof(output_0x02)) {
                return -1;
            }
            memcpy(buffer, output_0x02, sizeof(output_0x02));
            return sizeof(output_0x02);
        case PS4AuthReport::PS4_DEFINITION:
            if (reqlen < sizeof(controllerConfig)) {
                return -1;
            }
            controllerConfig.controllerType = (uint8_t)controllerType;
            memcpy(buffer, &controllerConfig, sizeof(controllerConfig));
            return sizeof(controllerConfig);
        case PS4AuthReport::PS4_GET_MAC_ADDRESS:
            if (reqlen < sizeof(output_0x12)) {
                return -1;
            }
            memcpy(buffer, output_0x12, sizeof(output_0x12));
            return sizeof(output_0x12);
        case PS4AuthReport::PS4_GET_VERSION_DATE:
            if (reqlen < sizeof(output_0xa3)) {
                return -1;
            }
            memcpy(buffer, output_0xa3, sizeof(output_0xa3));
            return sizeof(output_0xa3);
        // Unknown feature report (same sentinel as PS4Driver::get_report)
        default:
            return (uint16_t)-1;
    };
}

// Only PS4 does anything with set report
void PS4BDriver::set_report(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
    if (( report_type != HID_REPORT_TYPE_FEATURE ) && ( report_type != HID_REPORT_TYPE_OUTPUT ))
        return;

    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        if (report_id == 0) {
            memcpy(&ps4Features, buffer, bufsize);
        }
    } else if (report_type == HID_REPORT_TYPE_FEATURE) {
        // PS4B mode uses PC host mode - authentication-related set_report requests are not needed
        // PC host typically doesn't send these authentication payloads
        // No action needed for feature reports in PC host mode
    }
}


// Only XboxOG and Xbox One use vendor control xfer cb
bool PS4BDriver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    return false;
}

const uint16_t * PS4BDriver::get_descriptor_string_cb(uint8_t index, uint16_t langid) {
    const char *value = (const char *)ps4_string_descriptors[index];
    return getStringDescriptor(value, index); // getStringDescriptor returns a static array
}

const uint8_t * PS4BDriver::get_descriptor_device_cb() {
    return deviceDescriptor;
}

const uint8_t * PS4BDriver::get_hid_descriptor_report_cb(uint8_t itf) {
    (void)itf;
    return ps4_report_descriptor;
}

const uint8_t * PS4BDriver::get_descriptor_configuration_cb(uint8_t index) {
    return ps4b_configuration_descriptor;
}

const uint8_t * PS4BDriver::get_descriptor_device_qualifier_cb() {
    return nullptr;
}

uint16_t PS4BDriver::GetJoystickMidValue() {
    return PS4_JOYSTICK_MID << 8;
}
