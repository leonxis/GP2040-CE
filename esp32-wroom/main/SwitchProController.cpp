#include "SwitchProController.h"
#include "NimBLEDevice.h"
#include "ble_uuid.h"

#include <string.h>

#define VID_SIG 2
#define VID     0x057E
#define PID     0x2009
#define PID_VER 0x0101

#define HID_RPT_ID_INPUT_STD             0x21
#define HID_RPT_ID_INPUT_NFC_FW_UPDATE   0x23
#define HID_RPT_ID_INPUT_FULL            0x30
#define HID_RPT_ID_INPUT_NFC_MCU         0x31
#define HID_RPT_ID_INPUT_UNKNOWN1        0x32
#define HID_RPT_ID_INPUT_UNKNOWN2        0x33
#define HID_RPT_ID_INPUT                 0x3F

#define HID_RPT_ID_OUTPUT_RUMBLE_SUBCMD  0x01
#define HID_RPT_ID_OUTPUT_RUMBLE         0x10
#define HID_RPT_ID_OUTPUT_REQ_NFC_OR_RUMBLE 0x11
#define HID_RPT_ID_OUTPUT_UNKNOWN        0x12

#define SUB_CMD_GET_STATE                 0x00
#define SUB_CMD_BT_MANUAL_PAIRING         0x01
#define SUB_CMD_REQ_DEVICE_INFO           0x02
#define SUB_CMD_SET_INPUT_RPT_MODE        0x03
#define SUB_CMD_TRIGGER_BTNS_TIME_ELAPSED 0x04
#define SUB_CMD_GET_PAGE_LIST_STATE       0x05
#define SUB_CMD_SET_HCI_STATE             0x06
#define SUB_CMD_RESET_PAIRING_INFO        0x07
#define SUB_CMD_SET_LOW_POWER_SHIPPING_STATE 0x08
#define SUB_CMD_SET_SPI_FLASH_READ        0x10
#define SUB_CMD_SET_SPI_FLASH_WRITE       0x11
#define SUB_CMD_SET_SPI_SECTOR_ERASE      0x12
#define SUB_CMD_SET_RESET_NFC             0x20
#define SUB_CMD_SET_SET_NFC_CONFIG        0x21
#define SUB_CMD_SET_SET_NFC_STATE         0x22
#define SUB_CMD_SET_PLAYER_LED            0x30
#define SUB_CMD_GET_PLAYER_LED            0x31
#define SUB_CMD_SET_HOME_LED              0x38
#define SUB_CMD_ENABLE_IMU                0x40
#define SUB_CMD_SET_IMU_SENSITIVITY       0x41
#define SUB_CMD_WRITE_IMU_REG             0x42
#define SUB_CMD_READ_IMU_REG              0x43
#define SUB_CMD_ENABLE_VIBRATION          0x48
#define SUB_CMD_GET_REGULATED_VOLTAGE     0x50

#define INPUT_REPORT_FULL_SIZE 48
#define INPUT_REPORT_STAND_SIZE 48

#define RPT_MODE_NFC_IR_CAM   0x0
#define RPT_MODE_NFC_IR_MCU   0x1
#define RPT_MODE_NFC_IR_DATA  0x2
#define RPT_MODE_IR_CAM       0x3
#define RPT_MODE_STANDARD     0x30
#define RPT_MODE_NFC_IR       0x31
#define RPT_MODE_33           0x33
#define RPT_MODE_35           0x35
#define RPT_MODE_SIMPLE_HID   0x3F

// Fixed power state: no battery/charging hardware on this board.
#define FIXED_BATTERY_MV       4000
#define FIXED_BATTERY_LEVEL    4
#define FIXED_CHARGING         0

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
#define GP_A2  (1U << 13)

static const uint8_t reportmap[] = {
    0x05, 0x01,                                // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,                                // Usage (Game Pad)
    0xA1, 0x01,                                // Collection (Application)
    0x06, 0x01, 0xFF,                          //   Usage Page (Vendor Defined 0xFF01)
    0x85, HID_RPT_ID_INPUT_STD,                //   Report ID (33)
    0x09, 0x21,                                //   Usage (0x21)
    0x75, 0x08,                                //   Report Size (8)
    0x95, 0x30,                                //   Report Count (48)
    0x81, 0x02,                                //   Input (Data,Var,Abs)
    0x85, HID_RPT_ID_INPUT_FULL,               //   Report ID (48)
    0x09, 0x30,                                //   Usage (0x30)
    0x75, 0x08,                                //   Report Size (8)
    0x95, 0x30,                                //   Report Count (48)
    0x81, 0x02,                                //   Input (Data,Var,Abs)
    0x85, HID_RPT_ID_INPUT_NFC_MCU,            //   Report ID (49)
    0x09, 0x31,                                //   Usage (0x31)
    0x75, 0x08,                                //   Report Size (8)
    0x96, 0x69, 0x01,                          //   Report Count (361)
    0x81, 0x02,                                //   Input (Data,Var,Abs)
    0x85, HID_RPT_ID_INPUT_UNKNOWN1,           //   Report ID (50)
    0x09, 0x32,                                //   Usage (0x32)
    0x75, 0x08,                                //   Report Size (8)
    0x96, 0x69, 0x01,                          //   Report Count (361)
    0x81, 0x02,                                //   Input (Data,Var,Abs)
    0x85, HID_RPT_ID_INPUT_UNKNOWN2,           //   Report ID (51)
    0x09, 0x33,                                //   Usage (0x33)
    0x75, 0x08,                                //   Report Size (8)
    0x96, 0x69, 0x01,                          //   Report Count (361)
    0x81, 0x02,                                //   Input (Data,Var,Abs)
    0x85, HID_RPT_ID_INPUT,                    //   Report ID (63)
    0x05, 0x09,                                //   Usage Page (Button)
    0x19, 0x01,                                //   Usage Minimum (0x01)
    0x29, 0x10,                                //   Usage Maximum (0x10)
    0x15, 0x00,                                //   Logical Minimum (0)
    0x25, 0x01,                                //   Logical Maximum (1)
    0x75, 0x01,                                //   Report Size (1)
    0x95, 0x10,                                //   Report Count (16)
    0x81, 0x02,                                //   Input (Data,Var,Abs)
    0x05, 0x01,                                //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x39,                                //   Usage (Hat switch)
    0x15, 0x00,                                //   Logical Minimum (0)
    0x25, 0x07,                                //   Logical Maximum (7)
    0x75, 0x04,                                //   Report Size (4)
    0x95, 0x01,                                //   Report Count (1)
    0x81, 0x42,                                //   Input (Data,Var,Abs,Null State)
    0x05, 0x09,                                //   Usage Page (Button)
    0x75, 0x04,                                //   Report Size (4)
    0x95, 0x01,                                //   Report Count (1)
    0x81, 0x01,                                //   Input (Const,Array,Abs)
    0x05, 0x01,                                //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,                                //   Usage (X)
    0x09, 0x31,                                //   Usage (Y)
    0x09, 0x33,                                //   Usage (Rx)
    0x09, 0x34,                                //   Usage (Ry)
    0x16, 0x00, 0x00,                          //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00,              //   Logical Maximum (65534)
    0x75, 0x10,                                //   Report Size (16)
    0x95, 0x04,                                //   Report Count (4)
    0x81, 0x02,                                //   Input (Data,Var,Abs)
    0x06, 0x01, 0xFF,                          //   Usage Page (Vendor Defined 0xFF01)
    0x85, HID_RPT_ID_OUTPUT_RUMBLE_SUBCMD,     //   Report ID (1)
    0x09, 0x01,                                //   Usage (0x01)
    0x75, 0x08,                                //   Report Size (8)
    0x95, 0x30,                                //   Report Count (48)
    0x91, 0x02,                                //   Output (Data,Var,Abs,Non-volatile)
    0x85, HID_RPT_ID_OUTPUT_RUMBLE,            //   Report ID (16)
    0x09, 0x10,                                //   Usage (0x10)
    0x75, 0x08,                                //   Report Size (8)
    0x95, 0x30,                                //   Report Count (48)
    0x91, 0x02,                                //   Output (Data,Var,Abs,Non-volatile)
    0x85, HID_RPT_ID_OUTPUT_REQ_NFC_OR_RUMBLE, //   Report ID (17)
    0x09, 0x11,                                //   Usage (0x11)
    0x75, 0x08,                                //   Report Size (8)
    0x95, 0x30,                                //   Report Count (48)
    0x91, 0x02,                                //   Output (Data,Var,Abs,Non-volatile)
    0x85, HID_RPT_ID_OUTPUT_UNKNOWN,           //   Report ID (18)
    0x09, 0x12,                                //   Usage (0x12)
    0x75, 0x08,                                //   Report Size (8)
    0x95, 0x30,                                //   Report Count (48)
    0x91, 0x02,                                //   Output (Data,Var,Abs,Non-volatile)
    0xC0,                                      // End Collection
};

//0x6020~0x604E
static const uint8_t factoryCalibration[] = {
    //20~37 6-Axis motion sensor Factory calibration
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //Acc XYZ origin position when horizontal
    0x00, 0x40, 0x00, 0x40, 0x00, 0x40, //Acc XYZ sensitivity coeff (+-8G)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //Gyro XYZ origin position when still
    0x3b, 0x34, 0x3b, 0x34, 0x3b, 0x34, //Gyro XYZ sensitivity coeff (+-2000dps)
    //38~3C
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    //3D~4E left right stick calibration
    0xFF, 0xF7, 0x7F,
    0xFF, 0xF7, 0x7F,
    0xFF, 0xF7, 0x7F,
    0xFF, 0xF7, 0x7F,
    0xFF, 0xF7, 0x7F,
    0xFF, 0xF7, 0x7F,
};
//0x8010~0x8025 sticks calibration, 0x8026~0x803F motion sensor calibration
static uint8_t userCalibration[] = {
    0xB2, 0xA1,                         //left stick
    0xFF, 0xF7, 0x7F,
    0xFF, 0xF7, 0x7F,
    0xFF, 0xF7, 0x7F,
    0xB2, 0xA1,                         //right stick
    0xFF, 0xF7, 0x7F,
    0xFF, 0xF7, 0x7F,
    0xFF, 0xF7, 0x7F,
    0xB2, 0xA1,                         //Motion sensor calibration
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0x00, 0x40, 0x00, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3b, 0x34, 0x3b, 0x34, 0x3b, 0x34,
};

static void copyCalibration2Buf(uint16_t addr, uint8_t *buf, int16_t len)
{
    memset(buf, 0xFF, len);
    int16_t _addr;
    int16_t n;
    const uint8_t *calibration;
    if (addr & 0x8000)
    {
        _addr = addr - 0x8010;
        n = sizeof(userCalibration);
        calibration = userCalibration;
    }
    else
    {
        _addr = addr - 0x6020;
        n = sizeof(factoryCalibration);
        calibration = factoryCalibration;
    }
    if (_addr >= n)
        return;
    if ((_addr + len) <= 0)
        return;

    int16_t bufind0 = 0;
    int16_t startind = _addr;
    if (_addr < 0)
    {
        bufind0 = -_addr;
        startind = 0;
    }
    int16_t len0 = len - bufind0 + startind;
    len = len0 < len ? len0 : len;
    memcpy(&buf[bufind0], &calibration[startind], len);
}

static Input_Report ipacket;

SwitchProController::SwitchProController(const std::string &deviceName)
    : IMyGamepad(deviceName)
{
    ipRptMode = HID_RPT_ID_INPUT;

    int isNRpa;
    ble_hs_id_copy_addr(0, mac, &isNRpa);

    pHidDev->pnp(VID_SIG,
                 BUILD_UINT16(HI_UINT16(VID), LO_UINT16(VID)),
                 BUILD_UINT16(HI_UINT16(PID), LO_UINT16(PID)),
                 BUILD_UINT16(HI_UINT16(PID_VER), LO_UINT16(PID_VER)));
    pHidDev->hidInfo(0x00, 0x01);
    pHidDev->reportMap((uint8_t *)reportmap, sizeof(reportmap));

    _iRptChara = pHidDev->inputReport((uint8_t)HID_RPT_ID_INPUT);
    _iRptChara->setCallbacks(this);
    _iRptChara_STD = pHidDev->inputReport((uint8_t)HID_RPT_ID_INPUT_STD);
    _iRptChara_STD->setCallbacks(this);
    _iRptChara_Full = pHidDev->inputReport((uint8_t)HID_RPT_ID_INPUT_FULL);
    _iRptChara_Full->setCallbacks(this);
    _iRptChara_NFC_MCU = pHidDev->inputReport((uint8_t)HID_RPT_ID_INPUT_NFC_MCU);
    _iRptChara_NFC_MCU->setCallbacks(this);
    _iRptChara_Unknown1 = pHidDev->inputReport((uint8_t)HID_RPT_ID_INPUT_UNKNOWN1);
    _iRptChara_Unknown1->setCallbacks(this);
    _iRptChara_Unknown2 = pHidDev->inputReport((uint8_t)HID_RPT_ID_INPUT_UNKNOWN2);
    _iRptChara_Unknown2->setCallbacks(this);

    _oRptChara_Rumble_SubCMD = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_RUMBLE_SUBCMD);
    _oRptChara_Rumble_SubCMD->setCallbacks(this);
    _oRptChara_Rumble = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_RUMBLE);
    _oRptChara_Rumble->setCallbacks(this);
    _oRptChara_ReqNFC_Rumble = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_REQ_NFC_OR_RUMBLE);
    _oRptChara_ReqNFC_Rumble->setCallbacks(this);
    _oRptChara_Unknown = pHidDev->outputReport((uint8_t)HID_RPT_ID_OUTPUT_UNKNOWN);
    _oRptChara_Unknown->setCallbacks(this);
}

SwitchProController::~SwitchProController() {}

void SwitchProController::setSTDOrFullInputPacket(const InputSnapshot *snap)
{
    const uint16_t b = snap->buttons;

    // Switch physical layout: A/B and X/Y are swapped vs the GP layout.
    ipacket.btn.A = (b & GP_B2) != 0;
    ipacket.btn.B = (b & GP_B1) != 0;
    ipacket.btn.X = (b & GP_B4) != 0;
    ipacket.btn.Y = (b & GP_B3) != 0;
    ipacket.btn.L = (b & GP_L1) != 0;
    ipacket.btn.R = (b & GP_R1) != 0;
    ipacket.btn.LS = (b & GP_L3) != 0;
    ipacket.btn.RS = (b & GP_R3) != 0;
    ipacket.btn.Plus = (b & GP_S2) != 0;
    ipacket.btn.Home = (b & GP_A1) != 0;
    ipacket.btn.Minus = (b & GP_S1) != 0;
    ipacket.btn.Capture = (b & GP_A2) != 0;
    ipacket.btn.ZL = snap->lt > 0;
    ipacket.btn.ZR = snap->rt > 0;
    ipacket.btn.Up = (snap->dpad & 0x01) != 0;
    ipacket.btn.Down = (snap->dpad & 0x02) != 0;
    ipacket.btn.Left = (snap->dpad & 0x04) != 0;
    ipacket.btn.Right = (snap->dpad & 0x08) != 0;

    // 16-bit axis -> 12-bit Switch encoding (Y inverted).
    uint16_t lh = (uint16_t)(((uint32_t)snap->lx * 4095U) / 65535U);
    uint16_t lv = (uint16_t)(4095U - ((uint32_t)snap->ly * 4095U) / 65535U);
    uint16_t rh = (uint16_t)(((uint32_t)snap->rx * 4095U) / 65535U);
    uint16_t rv = (uint16_t)(4095U - ((uint32_t)snap->ry * 4095U) / 65535U);

    ipacket.stick.left_H_LB_8 = lh & 0xFF;
    ipacket.stick.left_H_HB_4 = (lh >> 8) & 0x0F;
    ipacket.stick.left_V_LB_4 = lv & 0x0F;
    ipacket.stick.left_V_HB_8 = (lv >> 4) & 0xFF;
    ipacket.stick.right_H_LB_8 = rh & 0xFF;
    ipacket.stick.right_H_HB_4 = (rh >> 8) & 0x0F;
    ipacket.stick.right_V_LB_4 = rv & 0x0F;
    ipacket.stick.right_V_HB_8 = (rv >> 4) & 0xFF;

    ipacket.motorStatus = (motorL > 0 || motorR > 0) ? 1 : 0;

    // No motion sensor on this board: IMU report slots stay zero-filled.
    memset(&ipacket.imuData0, 0, sizeof(ipacket.imuData0));
    memset(&ipacket.imuData1, 0, sizeof(ipacket.imuData1));
    memset(&ipacket.imuData2, 0, sizeof(ipacket.imuData2));
}

void SwitchProController::update(const InputSnapshot *snap)
{
    switch (ipRptMode)
    {
    case HID_RPT_ID_INPUT_FULL:
    {
        setSTDOrFullInputPacket(snap);
        packetTimer += 2;
        this->sendPacket(_iRptChara_Full, INPUT_REPORT_FULL_SIZE);
        break;
    }
    case HID_RPT_ID_INPUT:
    {
        static Gamepad_Input_Pro ip = {};
        const uint16_t b = snap->buttons;

        // Simple HID report follows the report map layout (no A/B swap).
        ip.btnA = (b & GP_B1) != 0;
        ip.btnB = (b & GP_B2) != 0;
        ip.btnX = (b & GP_B3) != 0;
        ip.btnY = (b & GP_B4) != 0;
        ip.btnL = (b & GP_L1) != 0;
        ip.btnR = (b & GP_R1) != 0;
        ip.btnLS = (b & GP_L3) != 0;
        ip.btnRS = (b & GP_R3) != 0;
        ip.btnPlus = (b & GP_S2) != 0;
        ip.btnHome = (b & GP_A1) != 0;
        ip.btnMinus = (b & GP_S1) != 0;
        ip.btnCapture = (b & GP_A2) != 0;
        ip.btnZL = snap->lt > 0;
        ip.btnZR = snap->rt > 0;

        ip.x = snap->lx;
        ip.y = snap->ly;
        ip.z = snap->rx;
        ip.Rz = snap->ry;

        int hat = dirKey2DPadValue(snap->dpad & 0x01,
                                   snap->dpad & 0x08,
                                   snap->dpad & 0x02,
                                   snap->dpad & 0x04);
        ip.hat = hat == 0 ? 8 : hat - 1;

        if (_iRptChara->getSubscribedCount())
            _iRptChara->notify((uint8_t *)&ip, sizeof(ip), true,
                               BLE_HCI_LE_CONN_HANDLE_MAX + 1);
        break;
    }
    default:
        break;
    }
}

void SwitchProController::enableVibration(bool enable)
{
    if (!enable) {
        motorL = motorR = 0;
    }
}

void SwitchProController::handleOutputRptRumble(const Output_Rumble *data)
{
    // Rumble data is acknowledged in reports but no actuators are wired.
    // Levels tracked only so motorStatus stays consistent with host state.
    (void)data;
    motorL = 1;
    motorR = 1;
}

void SwitchProController::handleOutputRptRumbleSubCMD(const Output_Rumble_SubCMD *subcmdPacket)
{
    handleOutputRptRumble((Output_Rumble *)subcmdPacket);

    ipacket.subcmdReply = 0x80;
    ipacket.subcmdReplyID = subcmdPacket->subcmd;
    ipacket.data[0] = 0x03;

    switch (subcmdPacket->subcmd)
    {
    case SUB_CMD_GET_STATE:
        break;
    case SUB_CMD_BT_MANUAL_PAIRING:
        break;
    case SUB_CMD_REQ_DEVICE_INFO:
        ipacket.subcmdReply = 0x82;
        ipacket.data[0] = 0x03; //firmware
        ipacket.data[1] = 0x48; //version
        ipacket.data[2] = 0x03; //pro controller
        ipacket.data[3] = 0x02; //unknown
        //4~9 mac big endian
        ipacket.data[4] = mac[5];
        ipacket.data[5] = mac[4];
        ipacket.data[6] = mac[3];
        ipacket.data[7] = mac[2];
        ipacket.data[8] = mac[1];
        ipacket.data[9] = mac[0];
        ipacket.data[10] = 0x01;
        ipacket.data[11] = 0x01;
        break;
    case SUB_CMD_SET_INPUT_RPT_MODE:
        switch (subcmdPacket->subcmd03.rptMode)
        {
        case RPT_MODE_NFC_IR_CAM:
        case RPT_MODE_NFC_IR_MCU:
        case RPT_MODE_NFC_IR_DATA:
        case RPT_MODE_IR_CAM:
            break;
        case RPT_MODE_STANDARD:
            ipRptMode = HID_RPT_ID_INPUT_FULL;
            break;
        case RPT_MODE_NFC_IR:
            break;
        case RPT_MODE_SIMPLE_HID:
            ipRptMode = HID_RPT_ID_INPUT;
            break;
        default:
            break;
        }
        break;
    case SUB_CMD_TRIGGER_BTNS_TIME_ELAPSED:
        break;
    case SUB_CMD_GET_PAGE_LIST_STATE:
        break;
    case SUB_CMD_SET_HCI_STATE:
        break;
    case SUB_CMD_RESET_PAIRING_INFO:
        break;
    case SUB_CMD_SET_LOW_POWER_SHIPPING_STATE:
        break;
    case SUB_CMD_SET_SPI_FLASH_READ:
    {
        uint16_t spiaddr = (uint16_t)subcmdPacket->subcmd10.address;
        ipacket.subcmdReply = 0x90;
        ipacket.data[0] = subcmdPacket->raw[0];
        ipacket.data[1] = subcmdPacket->raw[1];
        ipacket.data[2] = 0x00;
        ipacket.data[3] = 0x00;
        ipacket.data[4] = subcmdPacket->subcmd10.length;
        copyCalibration2Buf(spiaddr, &ipacket.data[5],
                            subcmdPacket->subcmd10.length);
        break;
    }
    case SUB_CMD_SET_SPI_FLASH_WRITE:
        break;
    case SUB_CMD_SET_SPI_SECTOR_ERASE:
        break;
    case SUB_CMD_SET_RESET_NFC:
        break;
    case SUB_CMD_SET_SET_NFC_CONFIG:
        break;
    case SUB_CMD_SET_SET_NFC_STATE:
        break;
    case 0x2A:
        break;
    case SUB_CMD_SET_PLAYER_LED:
        // Player LEDs not present; acknowledge only.
        break;
    case SUB_CMD_GET_PLAYER_LED:
        break;
    case SUB_CMD_SET_HOME_LED:
        // Home LED not present; acknowledge only.
        break;
    case SUB_CMD_ENABLE_IMU:
        isIMUEnable = subcmdPacket->subcmd40.enable;
        break;
    case SUB_CMD_SET_IMU_SENSITIVITY:
        break;
    case SUB_CMD_WRITE_IMU_REG:
        break;
    case SUB_CMD_READ_IMU_REG:
        break;
    case SUB_CMD_ENABLE_VIBRATION:
        enableVibration(subcmdPacket->subcmd48.enable != 0);
        break;
    case SUB_CMD_GET_REGULATED_VOLTAGE:
        ipacket.subcmdReply = 0xD0;
        ipacket.data[0] = (uint8_t)(FIXED_BATTERY_MV & 0xFF);
        ipacket.data[1] = (uint8_t)(FIXED_BATTERY_MV >> 8);
        break;
    default:
        break;
    }
    this->sendPacket(_iRptChara_STD, INPUT_REPORT_STAND_SIZE);
}

void SwitchProController::sendPacket(NimBLECharacteristic *chara, uint16_t len)
{
    packetTimer++;
    ipacket.timer = packetTimer;
    ipacket.charging = FIXED_CHARGING;
    ipacket.batteryLevel = FIXED_BATTERY_LEVEL;
    if (chara->getSubscribedCount())
        chara->notify((uint8_t *)&ipacket, len, true,
                      BLE_HCI_LE_CONN_HANDLE_MAX + 1);
}

void SwitchProController::onWrite(NimBLECharacteristic *pCharacteristic,
                                  NimBLEConnInfo &connInfo)
{
    (void)connInfo;
    uint8_t id = getRptID(pCharacteristic);
    const NimBLEAttValue &attv = pCharacteristic->getValue(nullptr);
    const uint8_t *data = attv.data();

    switch (id)
    {
    case HID_RPT_ID_OUTPUT_RUMBLE_SUBCMD:
        handleOutputRptRumbleSubCMD((const Output_Rumble_SubCMD *)data);
        break;
    case HID_RPT_ID_OUTPUT_RUMBLE:
        handleOutputRptRumble((const Output_Rumble *)data);
        break;
    default:
        break;
    }
}
