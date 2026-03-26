#pragma once

#include "drivers/xinput/XInputDriver.h"

class XInputBDriver : public XInputDriver {
public:
    void initialize() override;
    void initializeAux() override;
    void processAux() override;
    bool vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) override;
    const uint16_t * get_descriptor_string_cb(uint8_t index, uint16_t langid) override;
    const uint8_t * get_descriptor_device_cb() override;
    USBListener * get_usb_auth_listener() override;
};
