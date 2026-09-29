#ifndef _XBOX_ONE_GAMEPAD_H_
#define _XBOX_ONE_GAMEPAD_H_

#include "IMyGamepad.h"

#pragma pack(push, 1)
typedef struct
{
    unsigned short x;           // Usage 0x00010030: X, Value = 0 to 65535
    unsigned short y;           // Usage 0x00010031: Y, Value = 0 to 65535
    unsigned short z;           // Usage 0x00010032: Z, Value = 0 to 65535
    unsigned short Rz;          // Usage 0x00010035: Rz, Value = 0 to 65535
    unsigned short LT : 10;     // Usage 0x000200C5: Brake, Value = 0 to 1023
    unsigned short : 6;         // Pad
    unsigned short RT : 10;     // Usage 0x000200C4: Accelerator, Value = 0 to 1023
    unsigned short : 6;         // Pad
    unsigned char hat : 4;      // Usage 0x00010039: Hat switch, Value = 1 to 8
    unsigned char : 4;          // Pad
    unsigned char btnA : 1;     // Button 1
    unsigned char btnB : 1;     // Button 2
    unsigned char btn3 : 1;     // Button 3
    unsigned char btnX : 1;     // Button 4
    unsigned char btnY : 1;     // Button 5
    unsigned char btn6 : 1;     // Button 6
    unsigned char btnL : 1;     // Button 7
    unsigned char btnR : 1;     // Button 8
    unsigned char btn9 : 1;     // Button 9
    unsigned char btn10 : 1;    // Button 10
    unsigned char btnBack : 1;  // Button 11
    unsigned char btnStart : 1; // Button 12
    unsigned char btnXbox : 1;  // Button 13
    unsigned char btnLS : 1;    // Button 14
    unsigned char btnRS : 1;    // Button 15
    unsigned char : 1;          // Pad
    unsigned char Record : 1;
    unsigned char : 7;          // Pad
} Gamepad_Input_Xbox;
typedef struct
{
    unsigned char enableAcutator : 4;
    unsigned char : 4;
    unsigned char lTMagnitude;
    unsigned char rTMagnitude;
    unsigned char strongMagnitude;
    unsigned char weakMagnitude;
    unsigned char duration;
    unsigned char startDelay;
    unsigned char loopCount;
} OutputVibration;
#pragma pack(pop)

class XboxOneGamepad : public IMyGamepad, public NimBLECharacteristicCallbacks
{
private:
    NimBLECharacteristic *_iRptChara;
    NimBLECharacteristic *_oRptChara;
    // Output reports are received but vibration is not wired on this board.
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override;

public:
    XboxOneGamepad(const std::string &deviceName);
    ~XboxOneGamepad();
    void update(const InputSnapshot *snap) override;
};

#endif
