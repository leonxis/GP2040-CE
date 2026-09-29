#ifndef _SWITCH_PRO_CONTROLLER_H_
#define _SWITCH_PRO_CONTROLLER_H_
#include "IMyGamepad.h"
#include "stdint.h"
#include "assert.h"

#pragma pack(push, 1)
typedef struct
{
    uint8_t btnA : 1;
    uint8_t btnB : 1;
    uint8_t btnX : 1;
    uint8_t btnY : 1;
    uint8_t btnL : 1;
    uint8_t btnR : 1;
    uint8_t btnZL : 1;
    uint8_t btnZR : 1;
    uint8_t btnMinus : 1;
    uint8_t btnPlus : 1;
    uint8_t btnLS : 1;
    uint8_t btnRS : 1;
    uint8_t btnHome : 1;
    uint8_t btnCapture : 1;
    uint8_t btn15 : 1;
    uint8_t : 1;            // Pad
    uint8_t hat : 4;        // 0 to 7 (8 = none is encoded per-report)
    uint8_t : 4;            // Pad

    uint16_t x;
    uint16_t y;
    uint16_t z;
    uint16_t Rz;
} Gamepad_Input_Pro;
static_assert(sizeof(Gamepad_Input_Pro) == 11, "struct wrong size");

typedef struct
{
    int16_t accelx;
    int16_t accely;
    int16_t accelz;
    int16_t gyrox;
    int16_t gyroy;
    int16_t gyroz;
} IMU_Data;
static_assert(sizeof(IMU_Data) == 12, "struct wrong size");

typedef struct
{
    uint8_t timer;            //00~FF
    uint8_t connInfo : 4;     //0
    uint8_t charging : 1;     //0,1
    uint8_t batteryLevel : 3; //0,2,4,6,8
    union
    {
        uint8_t btns[3];
        struct
        {
            uint8_t Y : 1;
            uint8_t X : 1;
            uint8_t B : 1;
            uint8_t A : 1;
            uint8_t RSR : 1;
            uint8_t RSL : 1;
            uint8_t R : 1;
            uint8_t ZR : 1;

            uint8_t Minus : 1;
            uint8_t Plus : 1;
            uint8_t RS : 1;
            uint8_t LS : 1;
            uint8_t Home : 1;
            uint8_t Capture : 1;
            uint8_t : 1;
            uint8_t ChargingGrip : 1;

            uint8_t Down : 1;
            uint8_t Up : 1;
            uint8_t Right : 1;
            uint8_t Left : 1;
            uint8_t LSR : 1;
            uint8_t LSL : 1;
            uint8_t L : 1;
            uint8_t ZL : 1;
        } btn;
    };
    union
    {
        uint8_t sticks[6];
        struct
        {
            uint8_t left_H_LB_8 : 8;
            uint8_t left_H_HB_4 : 4;
            uint8_t left_V_LB_4 : 4;
            uint8_t left_V_HB_8 : 8;
            uint8_t right_H_LB_8 : 8;
            uint8_t right_H_HB_4 : 4;
            uint8_t right_V_LB_4 : 4;
            uint8_t right_V_HB_8 : 8;
        } stick;
    };
    uint8_t motorStatus;
    union
    {
        struct
        {
            uint8_t subcmdReply; //msb 1 ACK,0 NACK, lsb 7 reply data type
            uint8_t subcmdReplyID;
            uint8_t data[34];
        };
        struct
        {
            IMU_Data imuData0;
            IMU_Data imuData1;
            IMU_Data imuData2;
        };
        uint8_t mcudata[36];
    };
    union
    {
        uint8_t nfc[313];
        uint8_t ir[313];
    };
} Input_Report;
static_assert(sizeof(Input_Report) == 361, "struct wrong size");

typedef struct
{
    uint8_t cmd;
    uint8_t mac[6];
    uint8_t fixed[3];
    uint8_t alias[20];
    uint8_t extra[8];
} SubCMD_01;
static_assert(sizeof(SubCMD_01) == 38, "struct wrong size");

typedef struct
{
    uint8_t rptMode;
    uint8_t data[35];
    uint8_t crc;
    uint8_t tail;
} SubCMD_03;
static_assert(sizeof(SubCMD_03) == 38, "struct wrong size");

typedef struct
{
    uint16_t time;
} SubCMD_04;

typedef struct
{
    uint8_t hciMode;
} SubCMD_06;

typedef struct
{
    uint8_t enable;
} SubCMD_08;
typedef struct
{
    uint32_t address;
    uint8_t length;
} SubCMD_10;
typedef struct
{
    uint32_t address;
    uint8_t length;
    uint8_t data[29];
} SubCMD_11;
static_assert(sizeof(SubCMD_11) == 34, "struct wrong size");

typedef struct
{
    uint32_t address;
    uint8_t length;
} SubCMD_12;
typedef struct
{
    uint8_t state;
} SubCMD_22;
typedef struct
{
    uint8_t player : 4;
    uint8_t flashing : 4;
} SubCMD_30;
static_assert(sizeof(SubCMD_30) == 1, "struct wrong size");

typedef struct
{
    uint8_t base_duration : 4;
    uint8_t pattern_count : 4;
    uint8_t repeat_count : 4;
    uint8_t start_intensity : 4;
    uint8_t patterns[23];
} SubCMD_38;
static_assert(sizeof(SubCMD_38) == 25, "struct wrong size");

typedef struct
{
    uint8_t enable;
} SubCMD_40;
typedef struct
{
    uint8_t gyroRange;
    uint8_t accelRange;
    uint8_t gyroSampleRate;
    uint8_t accelBandwidth;
} SubCMD_41;
typedef struct
{
    uint8_t address;
    uint8_t operation;
    uint8_t value;
} SubCMD_42;
typedef struct
{
    uint8_t address;
    uint8_t count;
} SubCMD_43;
typedef struct
{
    uint8_t enable;
} SubCMD_48;
typedef struct
{
    uint16_t voltage_mv;
} SubCMD_50;
typedef struct
{
    uint8_t freq_h;
    uint8_t freq_h_amp;
    uint8_t freq_l;
    uint8_t freq_l_amp;
} Rumble_Data;
typedef struct
{
    uint8_t packetNum; //00~0F
    struct
    {
        Rumble_Data lRumble;
        Rumble_Data rRumble;
    } rumble;
} Output_Rumble;
static_assert(sizeof(Output_Rumble) == 9, "struct wrong size");

typedef struct
{
    uint8_t packetNum; //00~0F
    struct
    {
        Rumble_Data lRumble;
        Rumble_Data rRumble;
    } rumble;
    uint8_t subcmd;
    union
    {
        uint8_t raw[38];
        SubCMD_01 subcmd01;
        SubCMD_03 subcmd03;
        SubCMD_04 subcmd04;
        SubCMD_06 subcmd06;
        SubCMD_08 subcmd08;
        SubCMD_10 subcmd10;
        SubCMD_11 subcmd11;
        SubCMD_12 subcmd12;
        SubCMD_22 subcmd22;
        SubCMD_30 subcmd30;
        SubCMD_38 subcmd38;
        SubCMD_40 subcmd40;
        SubCMD_41 subcmd41;
        SubCMD_42 subcmd42;
        SubCMD_43 subcmd43;
        SubCMD_48 subcmd48;
    };
} Output_Rumble_SubCMD;
static_assert(sizeof(Output_Rumble_SubCMD) == 48, "struct wrong size");

#pragma pack(pop)

class SwitchProController : public IMyGamepad, public NimBLECharacteristicCallbacks
{
private:
    NimBLECharacteristic *_iRptChara;
    NimBLECharacteristic *_iRptChara_STD;
    NimBLECharacteristic *_iRptChara_Full;
    NimBLECharacteristic *_iRptChara_NFC_MCU;
    NimBLECharacteristic *_iRptChara_Unknown1;
    NimBLECharacteristic *_iRptChara_Unknown2;
    NimBLECharacteristic *_oRptChara_Rumble_SubCMD;
    NimBLECharacteristic *_oRptChara_Rumble;
    NimBLECharacteristic *_oRptChara_ReqNFC_Rumble;
    NimBLECharacteristic *_oRptChara_Unknown;
    uint8_t mac[6] = {};
    uint8_t packetTimer = 0;

    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override;

    uint8_t ipRptMode;
    uint8_t isIMUEnable = 0;
    // Tracked rumble levels (no physical actuators on this board).
    uint8_t motorL = 0, motorR = 0;

    void enableVibration(bool enable);
    void handleOutputRptRumble(const Output_Rumble *data);
    void handleOutputRptRumbleSubCMD(const Output_Rumble_SubCMD *subcmdPacket);
    void sendPacket(NimBLECharacteristic *chara, uint16_t len);
    void setSTDOrFullInputPacket(const InputSnapshot *snap);

public:
    SwitchProController(const std::string &deviceName);
    ~SwitchProController();
    void update(const InputSnapshot *snap) override;
};

#endif
