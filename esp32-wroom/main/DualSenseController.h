#ifndef _DUAL_SENSE_CONTROLLER_H_
#define _DUAL_SENSE_CONTROLLER_H_
#include "IMyGamepad.h"
#include "stdint.h"
#include "assert.h"

#pragma pack(push, 1)

enum Direction : uint8_t
{
    Direction_North = 0,
    Direction_NorthEast,
    Direction_East,
    Direction_SouthEast,
    Direction_South,
    Direction_SouthWest,
    Direction_West,
    Direction_NorthWest,
    Direction_None = 8
};

enum PowerState : uint8_t
{
    PowerState_Discharging = 0x00,
    PowerState_Charging = 0x01,
    PowerState_Complete = 0x02,
    PowerState_AbnormalVoltage = 0x0A,
    PowerState_AbnormalTemperature = 0x0B,
    PowerState_ChargingError = 0x0F
};

template <int N>
struct BTCRC
{
    uint8_t Buff[N - 4];
    uint32_t CRC;
};

struct TouchFingerData
{
    uint32_t Index : 7;
    uint32_t NotTouching : 1;
    uint32_t FingerX : 12;
    uint32_t FingerY : 12;
};
static_assert(sizeof(TouchFingerData) == 4, "struct wrong size");

struct TouchData
{
    TouchFingerData Finger[2];
    uint8_t Timestamp;
};
static_assert(sizeof(TouchData) == 9, "struct wrong size");

struct BTSimpleGetStateData
{ // 9
    uint8_t LeftStickX;
    uint8_t LeftStickY;
    uint8_t RightStickX;
    uint8_t RightStickY;
    Direction DPad : 4;
    uint8_t ButtonSquare : 1;
    uint8_t ButtonCross : 1;
    uint8_t ButtonCircle : 1;
    uint8_t ButtonTriangle : 1;
    uint8_t ButtonL1 : 1;
    uint8_t ButtonR1 : 1;
    uint8_t ButtonL2 : 1;
    uint8_t ButtonR2 : 1;
    uint8_t ButtonShare : 1;
    uint8_t ButtonOptions : 1;
    uint8_t ButtonL3 : 1;
    uint8_t ButtonR3 : 1;
    uint8_t ButtonHome : 1; // 6.1
    uint8_t ButtonPad : 1;  // 6.2
    uint8_t Counter : 6;    // 6.3
    uint8_t TriggerLeft;
    uint8_t TriggerRight;
};
static_assert(sizeof(BTSimpleGetStateData) == 9, "struct wrong size");

struct USBGetStateData
{ // 63
    uint8_t LeftStickX;
    uint8_t LeftStickY;
    uint8_t RightStickX;
    uint8_t RightStickY;
    uint8_t TriggerLeft;
    uint8_t TriggerRight;
    uint8_t SeqNo;
    Direction DPad : 4;
    uint8_t ButtonSquare : 1;
    uint8_t ButtonCross : 1;
    uint8_t ButtonCircle : 1;
    uint8_t ButtonTriangle : 1;
    uint8_t ButtonL1 : 1;
    uint8_t ButtonR1 : 1;
    uint8_t ButtonL2 : 1;
    uint8_t ButtonR2 : 1;
    uint8_t ButtonCreate : 1;
    uint8_t ButtonOptions : 1;
    uint8_t ButtonL3 : 1;
    uint8_t ButtonR3 : 1;
    uint8_t ButtonHome : 1;
    uint8_t ButtonPad : 1;
    uint8_t ButtonMute : 1;
    uint8_t UNK1 : 1;
    uint8_t ButtonLeftFunction : 1;
    uint8_t ButtonRightFunction : 1;
    uint8_t ButtonLeftPaddle : 1;
    uint8_t ButtonRightPaddle : 1;
    uint8_t UNK2;
    uint32_t UNK_COUNTER;
    int16_t AngularVelocityX;
    int16_t AngularVelocityZ;
    int16_t AngularVelocityY;
    int16_t AccelerometerX;
    int16_t AccelerometerY;
    int16_t AccelerometerZ;
    uint32_t SensorTimestamp;
    int8_t Temperature;
    struct TouchData TouchData;
    uint8_t TriggerRightStopLocation : 4;
    uint8_t TriggerRightStatus : 4;
    uint8_t TriggerLeftStopLocation : 4;
    uint8_t TriggerLeftStatus : 4;
    uint32_t HostTimestamp;
    uint8_t TriggerRightEffect : 4;
    uint8_t TriggerLeftEffect : 4;
    uint32_t DeviceTimeStamp;
    uint8_t PowerPercent : 4;
    enum PowerState PowerState : 4;
    uint8_t PluggedHeadphones : 1;
    uint8_t PluggedMic : 1;
    uint8_t MicMuted : 1;
    uint8_t PluggedUsbData : 1;
    uint8_t PluggedUsbPower : 1;
    uint8_t PluggedUnk1 : 3;
    uint8_t PluggedExternalMic : 1;
    uint8_t HapticLowPassFilter : 1;
    uint8_t PluggedUnk3 : 6;
    uint8_t AesCmac[8];
};
static_assert(sizeof(USBGetStateData) == 63, "struct wrong size");

struct BTGetStateData
{
    struct USBGetStateData StateData;
    uint8_t UNK1;
    uint8_t BtCrcFailCount;
};
static_assert(sizeof(BTGetStateData) == 65, "struct wrong size");

struct SetStateData
{ // 47
    uint8_t EnableRumbleEmulation : 1;
    uint8_t UseRumbleNotHaptics : 1;
    uint8_t AllowRightTriggerFFB : 1;
    uint8_t AllowLeftTriggerFFB : 1;
    uint8_t AllowHeadphoneVolume : 1;
    uint8_t AllowSpeakerVolume : 1;
    uint8_t AllowMicVolume : 1;
    uint8_t AllowAudioControl : 1;

    uint8_t AllowMuteLight : 1;
    uint8_t AllowAudioMute : 1;
    uint8_t AllowLedColor : 1;
    uint8_t ResetLights : 1;
    uint8_t AllowPlayerIndicators : 1;
    uint8_t AllowHapticLowPassFilter : 1;
    uint8_t AllowMotorPowerLevel : 1;
    uint8_t AllowAudioControl2 : 1;

    uint8_t RumbleEmulationRight;
    uint8_t RumbleEmulationLeft;
    uint8_t VolumeHeadphones;
    uint8_t VolumeSpeaker;
    uint8_t VolumeMic;
    uint8_t : 2;        // MicSelect
    uint8_t EchoCancelEnable : 1;
    uint8_t NoiseCancelEnable : 1;
    uint8_t : 2;        // OutputPathSelect
    uint8_t : 2;        // InputPathSelect
    uint8_t MuteLightMode;
    uint8_t TouchPowerSave : 1;
    uint8_t MotionPowerSave : 1;
    uint8_t HapticPowerSave : 1;
    uint8_t AudioPowerSave : 1;
    uint8_t MicMute : 1;
    uint8_t SpeakerMute : 1;
    uint8_t HeadphoneMute : 1;
    uint8_t HapticMute : 1;
    uint8_t RightTriggerFFB[11];
    uint8_t LeftTriggerFFB[11];
    uint32_t HostTimestamp;
    uint8_t TriggerMotorPowerReduction : 4;
    uint8_t RumbleMotorPowerReduction : 4;
    uint8_t SpeakerCompPreGain : 3;
    uint8_t BeamformingEnable : 1;
    uint8_t UnkAudioControl2 : 4;
    uint8_t AllowLightBrightnessChange : 1;
    uint8_t AllowColorLightFadeAnimation : 1;
    uint8_t EnableImprovedRumbleEmulation : 1;
    uint8_t UNKBITC : 5;
    uint8_t HapticLowPassFilter : 1;
    uint8_t UNKBIT : 7;
    uint8_t UNKBYTE;
    uint8_t LightFadeAnimation;
    uint8_t LightBrightness;
    uint8_t PlayerLight1 : 1;
    uint8_t PlayerLight2 : 1;
    uint8_t PlayerLight3 : 1;
    uint8_t PlayerLight4 : 1;
    uint8_t PlayerLight5 : 1;
    uint8_t PlayerLightFade : 1;
    uint8_t PlayerLightUNK : 2;
    uint8_t LedRed;
    uint8_t LedGreen;
    uint8_t LedBlue;
};
static_assert(sizeof(SetStateData) == 47, "struct wrong size");

struct ReportIn01
{
    BTSimpleGetStateData State;
};
static_assert(sizeof(ReportIn01) == 9, "struct wrong size");

struct ReportIn31
{
    union
    {
        BTCRC<77> CRC;
        struct
        {
            uint8_t HasHID : 1;
            uint8_t HasMic : 1;
            uint8_t Unk1 : 2;
            uint8_t SeqNo : 4;
            BTGetStateData State;
        } Data;
    };
};
static_assert(sizeof(ReportIn31) == 77, "struct wrong size");

struct ReportOut31
{
    union
    {
        BTCRC<77> CRC;
        struct
        {
            uint8_t UNK1 : 1;
            uint8_t EnableHID : 1;
            uint8_t UNK2 : 1;
            uint8_t UNK3 : 1;
            uint8_t SeqNo : 4;
            SetStateData State;
        } Data;
    };
};
static_assert(sizeof(ReportOut31) == 77, "struct wrong size");

struct ReportFeature05
{
    union
    {
        BTCRC<40> CRC;
        struct
        {
            int16_t GyroPitchBias;
            int16_t GyroYawBias;
            int16_t GyroRollBias;
            int16_t GyroPitchPlus;
            int16_t GyroPitchMinus;
            int16_t GyroYawPlus;
            int16_t GyroYawMinus;
            int16_t GyroRollPlus;
            int16_t GyroRollMinus;
            int16_t GyroSpeedPlus;
            int16_t GyroSpeedMinus;
            int16_t AccelXPlus;
            int16_t AccelXMinus;
            int16_t AccelYPlus;
            int16_t AccelYMinus;
            int16_t AccelZPlus;
            int16_t AccelZMinus;
            int16_t Unknown;
        } Data;
    };
};
static_assert(sizeof(ReportFeature05) == 40, "struct wrong size");

struct ReportFeature09
{
    uint8_t ClientMac[6];
    uint8_t Hard08;
    uint8_t Hard25;
    uint8_t Hard00;
    uint8_t HostMac[6];
    uint8_t Pad[4];
};
static_assert(sizeof(ReportFeature09) == 19, "struct wrong size");

struct ReportFeature20
{
    union
    {
        BTCRC<63> CRC;
        struct
        {
            char BuildDate[11];
            char BuildTime[8];
            uint16_t FwType;
            uint16_t SwSeries;
            uint32_t HardwareInfo;
            uint32_t FirmwareVersion;
            char DeviceInfo[12];
            uint16_t UpdateVersion;
            char UpdateImageInfo;
            char UpdateUnk;
            uint32_t FwVersion1;
            uint32_t FwVersion2;
            uint32_t FwVersion3;
        };
    };
};
static_assert(sizeof(ReportFeature20) == 63, "struct wrong size");

#pragma pack(pop)

class DualSenseController : public IMyGamepad, public NimBLECharacteristicCallbacks
{
private:
    NimBLECharacteristic *_iRptChara01;
    NimBLECharacteristic *_iRptChara31;
    NimBLECharacteristic *_oRptChara31;
    NimBLECharacteristic *_oRptChara32;
    NimBLECharacteristic *_oRptChara33;
    NimBLECharacteristic *_oRptChara34;
    NimBLECharacteristic *_oRptChara35;
    NimBLECharacteristic *_oRptChara36;
    NimBLECharacteristic *_oRptChara37;
    NimBLECharacteristic *_oRptChara38;
    NimBLECharacteristic *_oRptChara39;
    NimBLECharacteristic *_fRptChara05;
    NimBLECharacteristic *_fRptChara08;
    NimBLECharacteristic *_fRptChara09;
    NimBLECharacteristic *_fRptChara20;
    NimBLECharacteristic *_fRptChara22;
    NimBLECharacteristic *_fRptChara80;
    NimBLECharacteristic *_fRptChara81;
    NimBLECharacteristic *_fRptChara82;
    NimBLECharacteristic *_fRptChara83;
    NimBLECharacteristic *_fRptCharaF0;
    NimBLECharacteristic *_fRptCharaF1;
    NimBLECharacteristic *_fRptCharaF2;
    uint8_t mac[6] = {};
    uint32_t hostTimeStamp = 0;
    uint8_t ipRptMode;

    void onRead(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override;
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override;

    void _initStaticCharaValues();
    void _handleReportOut31(const struct ReportOut31 *rpt);
    void _handleReportIn01(const InputSnapshot *snap);
    void _handleReportIn31(const InputSnapshot *snap);

public:
    DualSenseController(const std::string &deviceName);
    ~DualSenseController();
    void update(const InputSnapshot *snap) override;
};

#endif
