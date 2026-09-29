#include "DualSenseController.h"
#include "NimBLEDevice.h"
#include "ble_uuid.h"
#include "esp_timer.h"
#include "rom/crc.h"

#include <string.h>

#define VID_SIG 2
#define VID     0x054C
#define PID     0x0CE6
#define PID_VER 0x0101

#define HID_RPT_ID_INPUT_01 0x01
#define HID_RPT_ID_INPUT_31 0x31

#define HID_RPT_ID_OUTPUT_31 0x31
#define HID_RPT_ID_OUTPUT_32 0x32
#define HID_RPT_ID_OUTPUT_33 0x33
#define HID_RPT_ID_OUTPUT_34 0x34
#define HID_RPT_ID_OUTPUT_35 0x35
#define HID_RPT_ID_OUTPUT_36 0x36
#define HID_RPT_ID_OUTPUT_37 0x37
#define HID_RPT_ID_OUTPUT_38 0x38
#define HID_RPT_ID_OUTPUT_39 0x39

#define HID_RPT_ID_FEAT_05 0x05
#define HID_RPT_ID_FEAT_08 0x08
#define HID_RPT_ID_FEAT_09 0x09
#define HID_RPT_ID_FEAT_20 0x20
#define HID_RPT_ID_FEAT_22 0x22
#define HID_RPT_ID_FEAT_80 0x80
#define HID_RPT_ID_FEAT_81 0x81
#define HID_RPT_ID_FEAT_82 0x82
#define HID_RPT_ID_FEAT_83 0x83
#define HID_RPT_ID_FEAT_F0 0xF0
#define HID_RPT_ID_FEAT_F1 0xF1
#define HID_RPT_ID_FEAT_F2 0xF2

// Fixed power state: no battery hardware on this board.
#define FIXED_POWER_PERCENT  10        // 100 %
#define FIXED_POWER_STATE    PowerState_Discharging

// GP2040-CE button bits
#define GP_B1  (1U << 0)
#define GP_B2  (1U << 1)
#define GP_B3  (1U << 2)
#define GP_B4  (1U << 3)
#define GP_L1  (1U << 4)
#define GP_R1  (1U << 5)
#define GP_S1  (1U << 8)
#define GP_S2  (1U << 9)
#define GP_L3  (1U << 10)
#define GP_R3  (1U << 11)
#define GP_A1  (1U << 12)

static const uint8_t reportmap[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,       // Usage (Game Pad)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1)
    0x09, 0x30,       //   Usage (X)
    0x09, 0x31,       //   Usage (Y)
    0x09, 0x32,       //   Usage (Z)
    0x09, 0x35,       //   Usage (Rz)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x04,       //   Report Count (4)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x09, 0x39,       //   Usage (Hat switch)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x07,       //   Logical Maximum (7)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0x3B, 0x01, //   Physical Maximum (315)
    0x65, 0x14,       //   Unit (English Rotation)
    0x75, 0x04,       //   Report Size (4)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x42,       //   Input (Data,Var,Abs,Null State)
    0x65, 0x00,       //   Unit (None)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (0x01)
    0x29, 0x0E,       //   Usage Maximum (0x0E)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x0E,       //   Report Count (14)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x75, 0x06,       //   Report Size (6)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x01,       //   Input (Const,Array,Abs)
    0x05, 0x01,       //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x33,       //   Usage (Rx)
    0x09, 0x34,       //   Usage (Ry)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x02,       //   Report Count (2)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x06, 0x00, 0xFF, //   Usage Page (Vendor Defined 0xFF00)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x4D,       //   Report Count (77)
    0x85, 0x31,       //   Report ID (49)
    0x09, 0x31,       //   Usage (0x31)
    0x91, 0x02,       //   Output (Data,Var,Abs,Non-volatile)
    0x09, 0x3B,       //   Usage (0x3B)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x85, 0x32,       //   Report ID (50)
    0x09, 0x32,       //   Usage (0x32)
    0x95, 0x8D,       //   Report Count (141)
    0x91, 0x02,       //   Output (Data,Var,Abs,Non-volatile)
    0x85, 0x33,       //   Report ID (51)
    0x09, 0x33,       //   Usage (0x33)
    0x95, 0xCD,       //   Report Count (205)
    0x91, 0x02,       //   Output (Data,Var,Abs,Non-volatile)
    0x85, 0x34,       //   Report ID (52)
    0x09, 0x34,       //   Usage (0x34)
    0x96, 0x0D, 0x01, //   Report Count (269)
    0x91, 0x02,       //   Output (Data,Var,Abs,Non-volatile)
    0x85, 0x35,       //   Report ID (53)
    0x09, 0x35,       //   Usage (0x35)
    0x96, 0x4D, 0x01, //   Report Count (333)
    0x91, 0x02,       //   Output (Data,Var,Abs,Non-volatile)
    0x85, 0x36,       //   Report ID (54)
    0x09, 0x36,       //   Usage (0x36)
    0x96, 0x8D, 0x01, //   Report Count (397)
    0x91, 0x02,       //   Output (Data,Var,Abs,Non-volatile)
    0x85, 0x37,       //   Report ID (55)
    0x09, 0x37,       //   Usage (0x37)
    0x96, 0xCD, 0x01, //   Report Count (461)
    0x91, 0x02,       //   Output (Data,Var,Abs,Non-volatile)
    0x85, 0x38,       //   Report ID (56)
    0x09, 0x38,       //   Usage (0x38)
    0x96, 0x0D, 0x02, //   Report Count (525)
    0x91, 0x02,       //   Output (Data,Var,Abs,Non-volatile)
    0x85, 0x39,       //   Report ID (57)
    0x09, 0x39,       //   Usage (0x39)
    0x96, 0x22, 0x02, //   Report Count (546)
    0x91, 0x02,       //   Output (Data,Var,Abs,Non-volatile)
    0x06, 0x80, 0xFF, //   Usage Page (Vendor Defined 0xFF80)
    0x85, 0x05,       //   Report ID (5)
    0x09, 0x33,       //   Usage (0x33)
    0x95, 0x28,       //   Report Count (40)
    0xB1, 0x02,       //   Feature (Data,Var,Abs,Non-volatile)
    0x85, 0x08,       //   Report ID (8)
    0x09, 0x34,       //   Usage (0x34)
    0x95, 0x2F,       //   Report Count (47)
    0xB1, 0x02,       //   Feature (Data,Var,Abs,Non-volatile)
    0x85, 0x09,       //   Report ID (9)
    0x09, 0x24,       //   Usage (0x24)
    0x95, 0x13,       //   Report Count (19)
    0xB1, 0x02,       //   Feature (Data,Var,Abs,Non-volatile)
    0x85, 0x20,       //   Report ID (32)
    0x09, 0x26,       //   Usage (0x26)
    0x95, 0x3F,       //   Report Count (63)
    0xB1, 0x02,       //   Feature (Data,Var,Abs,Non-volatile)
    0x85, 0x22,       //   Report ID (34)
    0x09, 0x40,       //   Usage (0x40)
    0x95, 0x3F,       //   Report Count (63)
    0xB1, 0x02,       //   Feature (Data,Var,Abs,Non-volatile)
    0x85, 0x80,       //   Report ID (128)
    0x09, 0x28,       //   Usage (0x28)
    0x95, 0x3F,       //   Report Count (63)
    0xB1, 0x02,       //   Feature (Data,Var,Abs,Non-volatile)
    0x85, 0x81,       //   Report ID (129)
    0x09, 0x29,       //   Usage (0x29)
    0x95, 0x3F,       //   Report Count (63)
    0xB1, 0x02,       //   Feature (Data,Var,Abs,Non-volatile)
    0x85, 0x82,       //   Report ID (130)
    0x09, 0x2A,       //   Usage (0x2A)
    0x95, 0x09,       //   Report Count (9)
    0xB1, 0x02,       //   Feature (Data,Var,Abs,Non-volatile)
    0x85, 0x83,       //   Report ID (131)
    0x09, 0x2B,       //   Usage (0x2B)
    0x95, 0x3F,       //   Report Count (63)
    0xB1, 0x02,       //   Feature (Data,Var,Abs,Non-volatile)
    0x85, 0xF1,       //   Report ID (241)
    0x09, 0x31,       //   Usage (0x31)
    0x95, 0x3F,       //   Report Count (63)
    0xB1, 0x02,       //   Feature (Data,Var,Abs,Non-volatile)
    0x85, 0xF2,       //   Report ID (242)
    0x09, 0x32,       //   Usage (0x32)
    0x95, 0x0F,       //   Report Count (15)
    0xB1, 0x02,       //   Feature (Data,Var,Abs,Non-volatile)
    0x85, 0xF0,       //   Report ID (240)
    0x09, 0x30,       //   Usage (0x30)
    0x95, 0x3F,       //   Report Count (63)
    0xB1, 0x02,       //   Feature (Data,Var,Abs,Non-volatile)
    0xC0,             // End Collection
};

DualSenseController::DualSenseController(const std::string &deviceName)
    : IMyGamepad(deviceName)
{
    ipRptMode = HID_RPT_ID_INPUT_01;

    int isNRpa;
    ble_hs_id_copy_addr(0, mac, &isNRpa);

    pHidDev->pnp(VID_SIG,
                 BUILD_UINT16(HI_UINT16(VID), LO_UINT16(VID)),
                 BUILD_UINT16(HI_UINT16(PID), LO_UINT16(PID)),
                 BUILD_UINT16(HI_UINT16(PID_VER), LO_UINT16(PID_VER)));
    pHidDev->hidInfo(0x00, 0x01);
    pHidDev->reportMap((uint8_t *)reportmap, sizeof(reportmap));

    _iRptChara01 = pHidDev->inputReport((uint8_t)HID_RPT_ID_INPUT_01);
    _iRptChara01->setCallbacks(this);
    _iRptChara31 = pHidDev->inputReport((uint8_t)HID_RPT_ID_INPUT_31);
    _iRptChara31->setCallbacks(this);

    _oRptChara31 = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_31);
    _oRptChara31->setCallbacks(this);
    _oRptChara32 = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_32);
    _oRptChara33 = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_33);
    _oRptChara34 = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_34);
    _oRptChara35 = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_35);
    _oRptChara36 = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_36);
    _oRptChara37 = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_37);
    _oRptChara38 = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_38);
    _oRptChara39 = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_39);

    _fRptChara05 = pHidDev->featureReport((uint8_t)HID_RPT_ID_FEAT_05);
    _fRptChara05->setCallbacks(this);
    _fRptChara08 = pHidDev->featureReport((uint8_t)HID_RPT_ID_FEAT_08);
    _fRptChara09 = pHidDev->featureReport((uint8_t)HID_RPT_ID_FEAT_09);
    _fRptChara09->setCallbacks(this);
    _fRptChara20 = pHidDev->featureReport((uint8_t)HID_RPT_ID_FEAT_20);
    _fRptChara20->setCallbacks(this);
    _fRptChara22 = pHidDev->featureReport((uint8_t)HID_RPT_ID_FEAT_22);
    _fRptChara80 = pHidDev->featureReport((uint8_t)HID_RPT_ID_FEAT_80);
    _fRptChara81 = pHidDev->featureReport((uint8_t)HID_RPT_ID_FEAT_81);
    _fRptChara82 = pHidDev->featureReport((uint8_t)HID_RPT_ID_FEAT_82);
    _fRptChara83 = pHidDev->featureReport((uint8_t)HID_RPT_ID_FEAT_83);
    _fRptCharaF0 = pHidDev->featureReport((uint8_t)HID_RPT_ID_FEAT_F0);
    _fRptCharaF1 = pHidDev->featureReport((uint8_t)HID_RPT_ID_FEAT_F1);
    _fRptCharaF2 = pHidDev->featureReport((uint8_t)HID_RPT_ID_FEAT_F2);

    _initStaticCharaValues();
}

DualSenseController::~DualSenseController() {}

void DualSenseController::_initStaticCharaValues()
{
    // Calibration feature stays all-zero.
    struct ReportFeature05 f05 = {};
    _fRptChara05->setValue((uint8_t *)&f05, sizeof(struct ReportFeature05));

    // Controller/host MAC feature.
    struct ReportFeature09 f09 = {};
    f09.ClientMac[0] = mac[5];
    f09.ClientMac[1] = mac[4];
    f09.ClientMac[2] = mac[3];
    f09.ClientMac[3] = mac[2];
    f09.ClientMac[4] = mac[1];
    f09.ClientMac[5] = mac[0];
    _fRptChara09->setValue((uint8_t *)&f09, sizeof(struct ReportFeature09));

    // Hardware/firmware info feature.
    struct ReportFeature20 f20 = {};
    f20.HardwareInfo = 0x0000FF00;
    f20.UpdateVersion = 0x0458;
    _fRptChara20->setValue((uint8_t *)&f20, sizeof(struct ReportFeature20));
}

void DualSenseController::_handleReportIn01(const InputSnapshot *snap)
{
    static uint8_t counter = 0;
    struct ReportIn01 ip01 = {};

    const uint16_t b = snap->buttons;
    ip01.State.ButtonCross = (b & GP_B1) != 0;
    ip01.State.ButtonCircle = (b & GP_B2) != 0;
    ip01.State.ButtonSquare = (b & GP_B3) != 0;
    ip01.State.ButtonTriangle = (b & GP_B4) != 0;
    ip01.State.ButtonL1 = (b & GP_L1) != 0;
    ip01.State.ButtonR1 = (b & GP_R1) != 0;
    ip01.State.ButtonL2 = snap->lt > 0;
    ip01.State.ButtonR2 = snap->rt > 0;
    ip01.State.ButtonShare = (b & GP_S1) != 0;
    ip01.State.ButtonOptions = (b & GP_S2) != 0;
    ip01.State.ButtonL3 = (b & GP_L3) != 0;
    ip01.State.ButtonR3 = (b & GP_R3) != 0;
    ip01.State.ButtonHome = (b & GP_A1) != 0;
    ip01.State.ButtonPad = 0;

    // Triggers already 0..255 — direct pass-through.
    ip01.State.TriggerLeft = snap->lt;
    ip01.State.TriggerRight = snap->rt;

    // 16-bit sticks -> 8-bit.
    ip01.State.LeftStickX = (uint8_t)(snap->lx >> 8);
    ip01.State.LeftStickY = (uint8_t)(snap->ly >> 8);
    ip01.State.RightStickX = (uint8_t)(snap->rx >> 8);
    ip01.State.RightStickY = (uint8_t)(snap->ry >> 8);

    counter++;
    if (counter > 63) counter = 0;
    ip01.State.Counter = counter;

    uint8_t dir = dirKey2DPadValue(snap->dpad & 0x01,
                                   snap->dpad & 0x08,
                                   snap->dpad & 0x02,
                                   snap->dpad & 0x04);
    dir = dir == 0 ? 8 : dir - 1;
    ip01.State.DPad = (Direction)dir;

    if (_iRptChara01->getSubscribedCount()) {
        _iRptChara01->notify((uint8_t *)&ip01, sizeof(ip01), true,
                             BLE_HCI_LE_CONN_HANDLE_MAX + 1);
    }
}

void DualSenseController::_handleReportIn31(const InputSnapshot *snap)
{
    struct ReportIn31 ip31 = {};
    uint8_t dir;

    ip31.Data.HasHID = 1;
    ip31.Data.State.StateData.SeqNo = 0x01;

    const uint16_t b = snap->buttons;
    ip31.Data.State.StateData.ButtonCross = (b & GP_B1) != 0;
    ip31.Data.State.StateData.ButtonCircle = (b & GP_B2) != 0;
    ip31.Data.State.StateData.ButtonSquare = (b & GP_B3) != 0;
    ip31.Data.State.StateData.ButtonTriangle = (b & GP_B4) != 0;
    ip31.Data.State.StateData.ButtonL1 = (b & GP_L1) != 0;
    ip31.Data.State.StateData.ButtonR1 = (b & GP_R1) != 0;
    ip31.Data.State.StateData.ButtonL2 = snap->lt > 0;
    ip31.Data.State.StateData.ButtonR2 = snap->rt > 0;
    ip31.Data.State.StateData.ButtonCreate = (b & GP_S1) != 0;
    ip31.Data.State.StateData.ButtonOptions = (b & GP_S2) != 0;
    ip31.Data.State.StateData.ButtonL3 = (b & GP_L3) != 0;
    ip31.Data.State.StateData.ButtonR3 = (b & GP_R3) != 0;
    ip31.Data.State.StateData.ButtonHome = (b & GP_A1) != 0;
    ip31.Data.State.StateData.ButtonPad = 0;

    ip31.Data.State.StateData.TriggerLeft = snap->lt;
    ip31.Data.State.StateData.TriggerRight = snap->rt;

    ip31.Data.State.StateData.LeftStickX = (uint8_t)(snap->lx >> 8);
    ip31.Data.State.StateData.LeftStickY = (uint8_t)(snap->ly >> 8);
    ip31.Data.State.StateData.RightStickX = (uint8_t)(snap->rx >> 8);
    ip31.Data.State.StateData.RightStickY = (uint8_t)(snap->ry >> 8);

    dir = dirKey2DPadValue(snap->dpad & 0x01,
                           snap->dpad & 0x08,
                           snap->dpad & 0x02,
                           snap->dpad & 0x04);
    dir = dir == 0 ? 8 : dir - 1;
    ip31.Data.State.StateData.DPad = (Direction)dir;

    // No motion sensor on this board: IMU fields remain zero-filled.
    ip31.Data.State.StateData.SensorTimestamp = (uint32_t)esp_timer_get_time();

    ip31.Data.State.StateData.PowerPercent = FIXED_POWER_PERCENT;
    ip31.Data.State.StateData.PowerState = FIXED_POWER_STATE;
    ip31.Data.State.StateData.HostTimestamp = hostTimeStamp;

    uint8_t *data = (uint8_t *)&ip31;
    uint16_t hdr = 0x31A1;
    uint32_t crc;
    crc = crc32_le(0, (uint8_t *)&hdr, sizeof(hdr));
    crc = crc32_le(crc, data, sizeof(ip31) - sizeof(crc));
    ip31.CRC.CRC = crc;

    if (_iRptChara31->getSubscribedCount()) {
        _iRptChara31->notify((uint8_t *)&ip31, sizeof(ip31), true,
                             BLE_HCI_LE_CONN_HANDLE_MAX + 1);
    }
}

void DualSenseController::update(const InputSnapshot *snap)
{
    switch (ipRptMode)
    {
    case HID_RPT_ID_INPUT_01:
        _handleReportIn01(snap);
        break;
    case HID_RPT_ID_INPUT_31:
        _handleReportIn31(snap);
        break;
    default:
        break;
    }
}

void DualSenseController::onRead(NimBLECharacteristic *pCharacteristic,
                                 NimBLEConnInfo &connInfo)
{
    (void)connInfo;
    uint8_t id = getRptID(pCharacteristic);
    switch (id)
    {
    case HID_RPT_ID_FEAT_05:
        // Host reading calibration -> switch to full 0x31 reports.
        ipRptMode = HID_RPT_ID_INPUT_31;
        break;
    case HID_RPT_ID_FEAT_09:
    case HID_RPT_ID_FEAT_20:
        break;
    default:
        break;
    }
}

void DualSenseController::_handleReportOut31(const struct ReportOut31 *rpt)
{
    // Keep the host timestamp mirror; no actuators wired on this board.
    hostTimeStamp = rpt->Data.State.HostTimestamp;
}

void DualSenseController::onWrite(NimBLECharacteristic *pCharacteristic,
                                  NimBLEConnInfo &connInfo)
{
    (void)connInfo;
    uint8_t id = getRptID(pCharacteristic);
    const NimBLEAttValue &attv = pCharacteristic->getValue(nullptr);
    const uint8_t *data = attv.data();

    switch (id)
    {
    case HID_RPT_ID_OUTPUT_31:
        _handleReportOut31((const struct ReportOut31 *)data);
        break;
    default:
        break;
    }
}
