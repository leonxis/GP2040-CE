#include "drivers/xinputb/XInputBDriver.h"

#define XINPUTB_MS_OS_VENDOR_CODE 0x17

static const uint8_t xinputb_device_descriptor[] = {
    0x12, 0x01,
    0x00, 0x02,
    0x00,
    0x00, 0x00,
    0x40,
    0x8A, 0x2E,
    0x93, 0x02,
    0x14, 0x01,
    0x01, 0x02, 0x03, 0x01,
};

static const uint16_t xinputb_ms_os_string_desc[] = {
    0x0312,
    'M', 'S', 'F', 'T', '1', '0', '0',
    XINPUTB_MS_OS_VENDOR_CODE
};

void XInputBDriver::initialize() {
    XInputDriver::initialize();
}

void XInputBDriver::initializeAux() {
    // XINPUTB is PC-only and does not use Xbox authentication.
}

void XInputBDriver::processAux() {
    // No auth processing in XINPUTB.
}

USBListener * XInputBDriver::get_usb_auth_listener() {
    return nullptr;
}

const uint16_t * XInputBDriver::get_descriptor_string_cb(uint8_t index, uint16_t langid) {
    if (index == 0xEE) {
        return xinputb_ms_os_string_desc;
    }
    return XInputDriver::get_descriptor_string_cb(index, langid);
}

const uint8_t * XInputBDriver::get_descriptor_device_cb() {
    return xinputb_device_descriptor;
}

bool XInputBDriver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    if (request->bmRequestType_bit.direction != TUSB_DIR_IN) {
        return false;
    }

    if (request->bRequest != XINPUTB_MS_OS_VENDOR_CODE || request->wIndex != 0x0004) {
        return false;
    }

    static const uint8_t compat_ids[] = {
        0x28,0x00,0x00,0x00,
        0x00,0x01,
        0x04,0x00,
        0x01,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,
        0x01,
        'X','U','S','B','1','0',0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00
    };

    return tud_control_xfer(rhport, request, (void*)compat_ids, sizeof(compat_ids));
}
