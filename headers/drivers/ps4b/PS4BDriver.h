/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#ifndef _PS4B_DRIVER_H_
#define _PS4B_DRIVER_H_

#include "drivers/ps4/PS4Driver.h"  // PS4B inherits the shared PS4 report/process implementation
#include "drivers/ps4b/PS4BDescriptors.h"  // PS4B-specific descriptors

// Note: PS4B mode uses PC host mode (PS4_ID_EMULATION), which does not require PS4 console authentication
// PS4Auth is included only for PS4AuthReport enum definition, not for actual authentication
class PS4BDriver : public PS4Driver {
public:
    PS4BDriver(uint32_t type): PS4Driver(type) {}
    virtual void initialize();
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
    virtual void onInputReportComplete();
    virtual void onInputReportFailed();
    bool getAuthSent() override {
        // PS4B mode uses PC host mode, no authentication needed
        return false;
    }
    bool getDongleAuthRequired() override;
protected:
    bool reportReady() override;
    bool sendReport(void * report, uint16_t report_size) override;
};

#endif // _PS4B_DRIVER_H_
