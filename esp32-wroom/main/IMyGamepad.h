#ifndef _I_MY_GAMEPAD_H_
#define _I_MY_GAMEPAD_H_

#include "NimBLEHIDDevice.h"
#include "input_state.h"

// High-performance only: connection interval 6 units (7.5 ms).
#define DESIRED_MIN_CONN_INTERVAL 6
#define DESIRED_MAX_CONN_INTERVAL 6
#define DESIRED_SLAVE_LATENCY    0
#define SUPERVISION_TIMEOUT       300

#define MAX_ADVERTISING_TIME_MS   0   // 0 = advertise forever

#define BUILD_UINT16(loByte, hiByte) \
    ((uint16_t)(((loByte)&0x00FF) + (((hiByte)&0x00FF) << 8)))

#define HI_UINT16(a) (((a) >> 8) & 0xFF)
#define LO_UINT16(a) ((a)&0xFF)

class IMyGamepad : public NimBLEServerCallbacks {
private:
    NimBLEServer *pServer;
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override;
    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override;
    void onMTUChange(uint16_t MTU, NimBLEConnInfo &connInfo) override;
    uint32_t onPassKeyRequest() override;
    void onAuthenticationComplete(NimBLEConnInfo &connInfo) override;
    bool onConfirmPIN(uint32_t pin) override;

protected:
    NimBLEHIDDevice *pHidDev;
    NimBLECharacteristic *_battChara;
    // Convert up/right/down/left bits into a 1..8 hat value (0 = centered).
    int dirKey2DPadValue(int up, int right, int down, int left);

public:
    IMyGamepad(const std::string &deviceName);
    virtual ~IMyGamepad();

    // Encode and send the latest snapshot (if a host is subscribed).
    virtual void update(const InputSnapshot *snap) = 0;

    void start();
    void stop();
    size_t getConnectedCount();
    void clearBond();
};

uint8_t getRptID(NimBLECharacteristic *pCharacteristic);

#endif
