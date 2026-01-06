/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#ifndef _PS4B_DRIVER_H_
#define _PS4B_DRIVER_H_

#include "gpdriver.h"
#include "drivers/ps4/PS4Driver.h"  // Include PS4Driver.h for PS4AuthReport enum
#include "drivers/ps4b/PS4BDescriptors.h"  // Then include PS4BDescriptors for PS4B-specific descriptors

// Note: PS4B mode uses PC host mode (PS4_ID_EMULATION), which does not require PS4 console authentication
// PS4Auth is included only for PS4AuthReport enum definition, not for actual authentication
class PS4BDriver : public GPDriver {
public:
    PS4BDriver(uint32_t type): controllerType(type) {}
    virtual void initialize();
    virtual bool process(Gamepad * gamepad);
    virtual void initializeAux();
    virtual void processAux();
    virtual uint16_t get_report(uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen);
    virtual void set_report(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize);
    virtual bool vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);
    virtual const uint16_t * get_descriptor_string_cb(uint8_t index, uint16_t langid);
    virtual const uint8_t * get_descriptor_device_cb();
    virtual const uint8_t * get_hid_descriptor_report_cb(uint8_t itf) ;
    virtual const uint8_t * get_descriptor_configuration_cb(uint8_t index);
    virtual const uint8_t * get_descriptor_device_qualifier_cb();
    virtual uint16_t GetJoystickMidValue();
    virtual USBListener * get_usb_auth_listener();
    bool getAuthSent() { 
        // PS4B mode uses PC host mode, no authentication needed
        return false; 
    }
    bool getDongleAuthRequired();
private:
    uint8_t last_report[CFG_TUD_ENDPOINT0_SIZE] = { };
    uint8_t last_report_counter;
    uint16_t last_axis_counter;
    PS4Report ps4Report;
    TouchpadData touchpadData;
    PSSensorData sensorData;
    uint32_t last_report_timer;
    PS4Auth * ps4AuthDriver;
    PS4AuthData * ps4AuthData;      // PS4 Authentication Data
    uint8_t cur_nonce_chunk;            // PS4 Encryption Nonce Chunk (Max 19)
    uint8_t cur_nonce_id;
    bool pointOneTouched = false;
    bool pointTwoTouched = false;
    uint8_t touchCounter;
    PS4FeatureOutputReport ps4Features;
    uint8_t lastFeatures[PS4_FEATURES_SIZE] = { };
    uint8_t deviceDescriptor[sizeof(ps4_device_descriptor)];
    bool authsent;
    PS4ControllerConfig controllerConfig;
    
    // Keyboard report for HID composite device
    uint8_t keyboard_modifier;
    uint8_t keyboard_reserved;
    uint8_t keyboard_keycode[6];
    uint8_t last_keyboard_report[8] = { }; // modifier(1) + reserved(1) + keycode[6] = 8 bytes

    InputModeDeviceType deviceType;

    // settings for controllerConfig
    uint32_t controllerType;        // PS4 DS4 / PS5 Third-Party
    bool enableController = true;
    bool enableMotion = true;
    bool enableLED = true;
    bool enableRumble = true;
    bool enableAnalog = false;
    bool enableUnknown0 = true;
    bool enableTouchpad = true;
    bool enableUnknown1 = true;

    // controller type bindings
    uint8_t shifterPosition = 0;
    const uint8_t shifterValues[PS4_MAX_GEARS] = {0,1,2,4,8,16,32,128};
    bool idleShifter = true;
    GamepadButtonMapping *buttonShiftUp;
    GamepadButtonMapping *buttonShiftDown;
    GamepadButtonMapping *buttonShift1;
    GamepadButtonMapping *buttonShift2;
    GamepadButtonMapping *buttonShift3;
    GamepadButtonMapping *buttonShift4;
    GamepadButtonMapping *buttonShift5;
    GamepadButtonMapping *buttonShift6;
    GamepadButtonMapping *buttonShiftR;
    GamepadButtonMapping *buttonShiftN;
    GamepadButtonMapping *buttonGas;
    GamepadButtonMapping *buttonBrake;
    GamepadButtonMapping *buttonClutch;
    GamepadButtonMapping *buttonSteerLeft;
    GamepadButtonMapping *buttonSteerRight;
    GamepadButtonMapping *buttonPlus;
    GamepadButtonMapping *buttonMinus;
    GamepadButtonMapping *buttonDialDown;
    GamepadButtonMapping *buttonDialUp;
    GamepadButtonMapping *buttonDialEnter;

    GamepadButtonMapping *buttonRudderLeft;
    GamepadButtonMapping *buttonRudderRight;
    GamepadButtonMapping *buttonThrottleForward;
    GamepadButtonMapping *buttonThrottleReverse;
    GamepadButtonMapping *buttonRockerLeft;
    GamepadButtonMapping *buttonRockerRight;
    GamepadButtonMapping *buttonPedalLeft;
    GamepadButtonMapping *buttonPedalRight;
    GamepadButtonMapping *buttonPedalRudderLeft;
    GamepadButtonMapping *buttonPedalRudderRight;

    GamepadButtonMapping *buttonFretGreen;
    GamepadButtonMapping *buttonFretRed;
    GamepadButtonMapping *buttonFretYellow;
    GamepadButtonMapping *buttonFretBlue;
    GamepadButtonMapping *buttonFretOrange;
    GamepadButtonMapping *buttonFretSoloGreen;
    GamepadButtonMapping *buttonFretSoloRed;
    GamepadButtonMapping *buttonFretSoloYellow;
    GamepadButtonMapping *buttonFretSoloBlue;
    GamepadButtonMapping *buttonFretSoloOrange;
    GamepadButtonMapping *buttonWhammy;
    GamepadButtonMapping *buttonPickup;
    GamepadButtonMapping *buttonTilt;

    GamepadButtonMapping *buttonDrumPadRed;
    GamepadButtonMapping *buttonDrumPadBlue;
    GamepadButtonMapping *buttonDrumPadYellow;
    GamepadButtonMapping *buttonDrumPadGreen;
    GamepadButtonMapping *buttonCymbalYellow;
    GamepadButtonMapping *buttonCymbalBlue;
    GamepadButtonMapping *buttonCymbalGreen;
    GamepadButtonMapping *buttonKickPedalLeft;
    GamepadButtonMapping *buttonKickPedalRight;
};

#endif // _PS4B_DRIVER_H_
