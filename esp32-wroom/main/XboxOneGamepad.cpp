#include "XboxOneGamepad.h"
#include "ble_uuid.h"

#define HID_RPT_ID_INPUT             0x01
#define HID_RPT_ID_OUTPUT_VIBRATION  0x03

#define VID_SIG 2   // 1=Bluetooth SIG, 2=USB SIG
#define VID     0x045E
#define PID     0x0B13
#define PID_VER 0x0509

// GP2040-CE button bits (lower 16)
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
    0x05, 0x01,                        //(GLOBAL) USAGE_PAGE         0x0001 Generic Desktop Page
    0x09, 0x05,                        //(LOCAL)  USAGE              0x00010005 Game Pad (Application Collection)
    0xA1, 0x01,                        //(MAIN)   COLLECTION         0x01 Application
    0x85, HID_RPT_ID_INPUT,            //  (GLOBAL) REPORT_ID          0x01 (1)
    0x09, 0x01,                        //  (LOCAL)  USAGE              0x00010001 Pointer (Physical Collection)
    0xA1, 0x00,                        //  (MAIN)   COLLECTION         0x00 Physical
    0x09, 0x30,                        //    (LOCAL)  USAGE              0x00010030 X (Dynamic Value)
    0x09, 0x31,                        //    (LOCAL)  USAGE              0x00010031 Y (Dynamic Value)
    0x15, 0x00,                        //    (GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00,      //    (GLOBAL) LOGICAL_MAXIMUM    0x0000FFFF (65535)
    0x95, 0x02,                        //    (GLOBAL) REPORT_COUNT       0x02 (2) Number of fields
    0x75, 0x10,                        //    (GLOBAL) REPORT_SIZE        0x10 (16) Number of bits per field
    0x81, 0x02,                        //    (MAIN)   INPUT              0x00000002 (2 fields x 16 bits)
    0xC0,                              //  (MAIN)   END_COLLECTION     Physical
    0x09, 0x01,                        //  (LOCAL)  USAGE              0x00010001 Pointer (Physical Collection)
    0xA1, 0x00,                        //  (MAIN)   COLLECTION         0x00 Physical
    0x09, 0x32,                        //    (LOCAL)  USAGE              0x00010032 Z (Dynamic Value)
    0x09, 0x35,                        //    (LOCAL)  USAGE              0x00010035 Rz (Dynamic Value)
    0x15, 0x00,                        //    (GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00,      //    (GLOBAL) LOGICAL_MAXIMUM    0x0000FFFF (65535)
    0x95, 0x02,                        //    (GLOBAL) REPORT_COUNT       0x02 (2) Number of fields
    0x75, 0x10,                        //    (GLOBAL) REPORT_SIZE        0x10 (16) Number of bits per field
    0x81, 0x02,                        //    (MAIN)   INPUT              0x00000002 (2 fields x 16 bits)
    0xC0,                              //  (MAIN)   END_COLLECTION     Physical
    0x05, 0x02,                        //(GLOBAL) USAGE_PAGE         0x0002 Simulation Controls Page
    0x09, 0xC5,                        //(LOCAL)  USAGE              0x000200C5 Brake (Dynamic Value)
    0x15, 0x00,                        //(GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x26, 0xFF, 0x03,                  //(GLOBAL) LOGICAL_MAXIMUM    0x03FF (1023)
    0x95, 0x01,                        //(GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x75, 0x0A,                        //(GLOBAL) REPORT_SIZE        0x0A (10) Number of bits per field
    0x81, 0x02,                        //(MAIN)   INPUT              0x00000002 (1 field x 10 bits)
    0x15, 0x00,                        //(GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x25, 0x00,                        //(GLOBAL) LOGICAL_MAXIMUM    0x00 (0)
    0x75, 0x06,                        //(GLOBAL) REPORT_SIZE        0x06 (6) Number of bits per field
    0x95, 0x01,                        //(GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x81, 0x03,                        //(MAIN)   INPUT              0x00000003 (1 field x 6 bits) 1=Constant
    0x05, 0x02,                        //(GLOBAL) USAGE_PAGE         0x0002 Simulation Controls Page
    0x09, 0xC4,                        //(LOCAL)  USAGE              0x000200C4 Accelerator (Dynamic Value)
    0x15, 0x00,                        //(GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x26, 0xFF, 0x03,                  //(GLOBAL) LOGICAL_MAXIMUM    0x03FF (1023)
    0x95, 0x01,                        //(GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x75, 0x0A,                        //(GLOBAL) REPORT_SIZE        0x0A (10) Number of bits per field
    0x81, 0x02,                        //(MAIN)   INPUT              0x00000002 (1 field x 10 bits)
    0x15, 0x00,                        //(GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x25, 0x00,                        //(GLOBAL) LOGICAL_MAXIMUM    0x00 (0)
    0x75, 0x06,                        //(GLOBAL) REPORT_SIZE        0x06 (6) Number of bits per field
    0x95, 0x01,                        //(GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x81, 0x03,                        //(MAIN)   INPUT              0x00000003 (1 field x 6 bits) 1=Constant
    0x05, 0x01,                        //(GLOBAL) USAGE_PAGE         0x0001 Generic Desktop Page
    0x09, 0x39,                        //(LOCAL)  USAGE              0x00010039 Hat switch (Dynamic Value)
    0x15, 0x01,                        //(GLOBAL) LOGICAL_MINIMUM    0x01 (1)
    0x25, 0x08,                        //(GLOBAL) LOGICAL_MAXIMUM    0x08 (8)
    0x35, 0x00,                        //(GLOBAL) PHYSICAL_MINIMUM   0x00 (0)
    0x46, 0x3B, 0x01,                  //(GLOBAL) PHYSICAL_MAXIMUM   0x013B (315)
    0x66, 0x14, 0x00,                  //(GLOBAL) UNIT               0x0014 Rotation in degrees [1° units]
    0x75, 0x04,                        //(GLOBAL) REPORT_SIZE        0x04 (4) Number of bits per field
    0x95, 0x01,                        //(GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x81, 0x42,                        //(MAIN)   INPUT              0x00000042 (1 field x 4 bits) 1=Null
    0x75, 0x04,                        //(GLOBAL) REPORT_SIZE        0x04 (4) Number of bits per field
    0x95, 0x01,                        //(GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x15, 0x00,                        //(GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x25, 0x00,                        //(GLOBAL) LOGICAL_MAXIMUM    0x00 (0)
    0x35, 0x00,                        //(GLOBAL) PHYSICAL_MINIMUM   0x00 (0)
    0x45, 0x00,                        //(GLOBAL) PHYSICAL_MAXIMUM   0x00 (0)
    0x65, 0x00,                        //(GLOBAL) UNIT               0x00 No unit (0=None)
    0x81, 0x03,                        //(MAIN)   INPUT              0x00000003 (1 field x 4 bits) 1=Constant
    0x05, 0x09,                        //(GLOBAL) USAGE_PAGE         0x0009 Button Page
    0x19, 0x01,                        //(LOCAL)  USAGE_MINIMUM      0x00090001 Button 1
    0x29, 0x0F,                        //(LOCAL)  USAGE_MAXIMUM      0x0009000F Button 15
    0x15, 0x00,                        //(GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x25, 0x01,                        //(GLOBAL) LOGICAL_MAXIMUM    0x01 (1)
    0x75, 0x01,                        //(GLOBAL) REPORT_SIZE        0x01 (1) Number of bits per field
    0x95, 0x0F,                        //(GLOBAL) REPORT_COUNT       0x0F (15) Number of fields
    0x81, 0x02,                        //(MAIN)   INPUT              0x00000002 (15 fields x 1 bit)
    0x15, 0x00,                        //(GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x25, 0x00,                        //(GLOBAL) LOGICAL_MAXIMUM    0x00 (0)
    0x75, 0x01,                        //(GLOBAL) REPORT_SIZE        0x01 (1) Number of bits per field
    0x95, 0x01,                        //(GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x81, 0x03,                        //(MAIN)   INPUT              0x00000003 (1 field x 1 bit) 1=Constant
    0x05, 0x0C,                        //(GLOBAL) USAGE_PAGE         0x000C Consumer Device Page
    0x0A, 0xB2, 0x00,                  //(LOCAL)  USAGE              0x000C00B2 Record (On/Off Control)
    0x15, 0x00,                        //(GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x25, 0x01,                        //(GLOBAL) LOGICAL_MAXIMUM    0x01 (1)
    0x95, 0x01,                        //(GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x75, 0x01,                        //(GLOBAL) REPORT_SIZE        0x01 (1) Number of bits per field
    0x81, 0x02,                        //(MAIN)   INPUT              0x00000002 (1 field x 1 bit)
    0x15, 0x00,                        //(GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x25, 0x00,                        //(GLOBAL) LOGICAL_MAXIMUM    0x00 (0)
    0x75, 0x07,                        //(GLOBAL) REPORT_SIZE        0x07 (7) Number of bits per field
    0x95, 0x01,                        //(GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x81, 0x03,                        //(MAIN)   INPUT              0x00000003 (1 field x 7 bits) 1=Constant
    0x05, 0x0F,                        //(GLOBAL) USAGE_PAGE         0x000F Physical Interface Device Page
    0x09, 0x21,                        //(LOCAL)  USAGE              0x000F0021 Set Effect Report (Logical Collection)
    0x85, HID_RPT_ID_OUTPUT_VIBRATION, //  (GLOBAL) REPORT_ID          0x03 (3)
    0xA1, 0x02,                        //  (MAIN)   COLLECTION         0x02 Logical
    0x09, 0x97,                        //    (LOCAL)  USAGE              0x000F0097 DC Enable Actuators (Selector)
    0x15, 0x00,                        //    (GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x25, 0x01,                        //    (GLOBAL) LOGICAL_MAXIMUM    0x01 (1)
    0x75, 0x04,                        //    (GLOBAL) REPORT_SIZE        0x04 (4) Number of bits per field
    0x95, 0x01,                        //    (GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x91, 0x02,                        //    (MAIN)   OUTPUT             0x00000002 (1 field x 4 bits)
    0x15, 0x00,                        //    (GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x25, 0x00,                        //    (GLOBAL) LOGICAL_MAXIMUM    0x00 (0)
    0x75, 0x04,                        //    (GLOBAL) REPORT_SIZE        0x04 (4) Number of bits per field
    0x95, 0x01,                        //    (GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x91, 0x03,                        //    (MAIN)   OUTPUT             0x00000003 (1 field x 4 bits) 1=Constant
    0x09, 0x70,                        //    (LOCAL)  USAGE              0x000F0070 Magnitude (Dynamic Value)
    0x15, 0x00,                        //    (GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x25, 0x64,                        //    (GLOBAL) LOGICAL_MAXIMUM    0x64 (100)
    0x75, 0x08,                        //    (GLOBAL) REPORT_SIZE        0x08 (8) Number of bits per field
    0x95, 0x04,                        //    (GLOBAL) REPORT_COUNT       0x04 (4) Number of fields
    0x91, 0x02,                        //    (MAIN)   OUTPUT             0x00000002 (4 fields x 8 bits)
    0x09, 0x50,                        //    (LOCAL)  USAGE              0x000F0050 Duration (Dynamic Value)
    0x66, 0x01, 0x10,                  //    (GLOBAL) UNIT               0x1001 Time in seconds
    0x55, 0x0E,                        //    (GLOBAL) UNIT_EXPONENT      0x0E (Unit Value x 10^-2)
    0x15, 0x00,                        //    (GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x26, 0xFF, 0x00,                  //    (GLOBAL) LOGICAL_MAXIMUM    0x00FF (255)
    0x75, 0x08,                        //    (GLOBAL) REPORT_SIZE        0x08 (8) Number of bits per field
    0x95, 0x01,                        //    (GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x91, 0x02,                        //    (MAIN)   OUTPUT             0x00000002 (1 field x 8 bits)
    0x09, 0xA7,                        //    (LOCAL)  USAGE              0x000F00A7 Start Delay (Dynamic Value)
    0x15, 0x00,                        //    (GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x26, 0xFF, 0x00,                  //    (GLOBAL) LOGICAL_MAXIMUM    0x00FF (255)
    0x75, 0x08,                        //    (GLOBAL) REPORT_SIZE        0x08 (8) Number of bits per field
    0x95, 0x01,                        //    (GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x91, 0x02,                        //    (MAIN)   OUTPUT             0x00000002 (1 field x 8 bits)
    0x65, 0x00,                        //(GLOBAL) UNIT               0x00 No unit (0=None)
    0x55, 0x00,                        //(GLOBAL) UNIT_EXPONENT      0x00 (Unit Value x 10^0)
    0x09, 0x7C,                        //(LOCAL)  USAGE              0x000F007C Loop Count (Dynamic Value)
    0x15, 0x00,                        //(GLOBAL) LOGICAL_MINIMUM    0x00 (0)
    0x26, 0xFF, 0x00,                  //(GLOBAL) LOGICAL_MAXIMUM    0x00FF (255)
    0x75, 0x08,                        //(GLOBAL) REPORT_SIZE        0x08 (8) Number of bits per field
    0x95, 0x01,                        //(GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x91, 0x02,                        //(MAIN)   OUTPUT             0x00000002 (1 field x 8 bits)
    0xC0,                              //(MAIN)   END_COLLECTION     Logical
    0xC0,                              //(MAIN)   END_COLLECTION     Application
};

XboxOneGamepad::XboxOneGamepad(const std::string &deviceName)
    : IMyGamepad(deviceName) {
    pHidDev->pnp(VID_SIG,
                 BUILD_UINT16(HI_UINT16(VID), LO_UINT16(VID)),
                 BUILD_UINT16(HI_UINT16(PID), LO_UINT16(PID)),
                 BUILD_UINT16(HI_UINT16(PID_VER), LO_UINT16(PID_VER)));
    pHidDev->hidInfo(0x00, 0x01);
    pHidDev->reportMap((uint8_t *)reportmap, sizeof(reportmap));

    _iRptChara = pHidDev->inputReport((uint8_t)HID_RPT_ID_INPUT);
    _oRptChara = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_VIBRATION);
    _oRptChara->setCallbacks(this);
}

XboxOneGamepad::~XboxOneGamepad() {}

void XboxOneGamepad::update(const InputSnapshot *snap) {
    static Gamepad_Input_Xbox ip = {};

    const uint16_t b = snap->buttons;
    ip.btnA     = (b & GP_B1) != 0;
    ip.btnB     = (b & GP_B2) != 0;
    ip.btnX     = (b & GP_B3) != 0;
    ip.btnY     = (b & GP_B4) != 0;
    ip.btnL     = (b & GP_L1) != 0;
    ip.btnR     = (b & GP_R1) != 0;
    ip.btnBack  = (b & GP_S1) != 0;
    ip.btnStart = (b & GP_S2) != 0;
    ip.btnLS    = (b & GP_L3) != 0;
    ip.btnRS    = (b & GP_R3) != 0;
    ip.btnXbox  = (b & GP_A1) != 0;

    // Sticks arrive already in 0..65535 — pass through unchanged.
    ip.x  = snap->lx;
    ip.y  = snap->ly;
    ip.z  = snap->rx;
    ip.Rz = snap->ry;

    // Linear triggers 0..255 -> 0..1023
    ip.LT = (uint16_t)(((uint32_t)snap->lt * 1023U) / 255U);
    ip.RT = (uint16_t)(((uint32_t)snap->rt * 1023U) / 255U);

    // dpad bitmask: bit0 up / bit1 down / bit2 left / bit3 right
    ip.hat = dirKey2DPadValue(snap->dpad & 0x01,        // up
                              snap->dpad & 0x08,        // right
                              snap->dpad & 0x02,        // down
                              snap->dpad & 0x04);       // left

    if (_iRptChara->getSubscribedCount()) {
        _iRptChara->notify((uint8_t *)&ip, sizeof(ip), true,
                           BLE_HCI_LE_CONN_HANDLE_MAX + 1);
    }
}

void XboxOneGamepad::onWrite(NimBLECharacteristic *pCharacteristic,
                             NimBLEConnInfo &connInfo) {
    (void)connInfo;
    // Vibration output channel retained for protocol compatibility;
    // no actuators wired on this board.
    if (pCharacteristic == _oRptChara) {
        // intentionally empty
    }
}
